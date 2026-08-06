#include <cstdint>
#include <string>

#include <windows.h>

#include "gti/engine.h"
#include "gti/hooks.h"
#include "gti/ipc_client.h"
#include "gti/log.h"
#include "gti/string_util.h"
#include "gti/text_filter.h"

namespace gti {
namespace {

struct MonoDomain;

using MonoStringNewFn = void* (__cdecl*)(MonoDomain*, const char*);
using MonoStringNewLenFn = void* (__cdecl*)(MonoDomain*, const char*, uint32_t);
using MonoStringNewWrapperFn = void* (__cdecl*)(const char*);

MonoStringNewFn g_origNew = nullptr;
MonoStringNewLenFn g_origNewLen = nullptr;
MonoStringNewWrapperFn g_origWrapper = nullptr;
thread_local bool g_inHook = false;

std::string TranslateOrEmpty(const std::string& source) {
    std::string reason;
    if (!gTextFilter.ShouldTranslate(source, &reason)) {
        IpcClient::Instance().skipped.fetch_add(1);
        return {};
    }
    IpcClient::Instance().captured.fetch_add(1);
    std::string target;
    std::string origin;
    if (IpcClient::Instance().RequestTranslation(source, EngineId(GtiEngine::UnityMono),
                                                 &target, &origin,
                                                 IpcClient::Instance().blockTimeoutMs()) &&
        !target.empty()) {
        IpcClient::Instance().translated.fetch_add(1);
        return target;
    }
    return {};
}

void* MonoStringNewImpl(MonoDomain* domain, const char* text) {
    std::string translated = TranslateOrEmpty(text);
    return g_origNew(domain, translated.empty() ? text : translated.c_str());
}

void* MonoStringNewWrapperImpl(const char* text) {
    std::string translated = TranslateOrEmpty(text);
    return g_origWrapper(translated.empty() ? text : translated.c_str());
}

void* MonoStringNewLenImpl(MonoDomain* domain, const char* text, uint32_t len) {
    std::string source(text, len);
    std::string translated = TranslateOrEmpty(source);
    return g_origNewLen(domain, translated.empty() ? text : translated.c_str(),
                        static_cast<uint32_t>(translated.size()));
}

void* __cdecl DetourMonoStringNew(MonoDomain* domain, const char* text) {
    if (!HooksActive() || g_inHook || !text) {
        return g_origNew(domain, text);
    }
    g_inHook = true;
    void* result = nullptr;
    __try {
        result = MonoStringNewImpl(domain, text);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        result = g_origNew(domain, text);
    }
    g_inHook = false;
    return result;
}

void* __cdecl DetourMonoStringNewWrapper(const char* text) {
    if (!HooksActive() || g_inHook || !text) {
        return g_origWrapper(text);
    }
    g_inHook = true;
    void* result = nullptr;
    __try {
        result = MonoStringNewWrapperImpl(text);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        result = g_origWrapper(text);
    }
    g_inHook = false;
    return result;
}

void* __cdecl DetourMonoStringNewLen(MonoDomain* domain, const char* text, uint32_t len) {
    if (!HooksActive() || g_inHook || !text) {
        return g_origNewLen(domain, text, len);
    }
    g_inHook = true;
    void* result = nullptr;
    __try {
        result = MonoStringNewLenImpl(domain, text, len);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        result = g_origNewLen(domain, text, len);
    }
    g_inHook = false;
    return result;
}

}  // namespace

bool InstallUnityMonoHooks() {
    HMODULE mono = GetModuleHandleW(L"mono-2.0-bdwgc.dll");
    if (!mono) {
        mono = GetModuleHandleW(L"mono.dll");
    }
    if (!mono) {
        return false;
    }
    void* fnNew = GetModuleExport(mono, "mono_string_new");
    void* fnNewLen = GetModuleExport(mono, "mono_string_new_len");
    void* fnWrapper = GetModuleExport(mono, "mono_string_new_wrapper");
    if (!fnNew && !fnNewLen && !fnWrapper) {
        Log(LogLevel::Warn, "mono string exports not found");
        return false;
    }
    bool ok = true;
    if (fnNew) {
        ok &= HookManager::Install("mono_string_new", fnNew,
                                   reinterpret_cast<void*>(&DetourMonoStringNew),
                                   reinterpret_cast<void**>(&g_origNew));
    }
    if (fnNewLen) {
        ok &= HookManager::Install("mono_string_new_len", fnNewLen,
                                   reinterpret_cast<void*>(&DetourMonoStringNewLen),
                                   reinterpret_cast<void**>(&g_origNewLen));
    }
    if (fnWrapper) {
        ok &= HookManager::Install("mono_string_new_wrapper", fnWrapper,
                                   reinterpret_cast<void*>(&DetourMonoStringNewWrapper),
                                   reinterpret_cast<void**>(&g_origWrapper));
    }
    return ok;
}

}  // namespace gti
