#include <cstdint>
#include <string>

#include <windows.h>

#include "gti/engine.h"
#include "gti/hooks.h"
#include "gti/ipc_client.h"
#include "gti/log.h"
#include "gti/signature_scan.h"
#include "gti/string_util.h"
#include "gti/text_filter.h"

namespace gti {
namespace {

using StringNewFn = void* (__cdecl*)(const char*);
using StringNewLenFn = void* (__cdecl*)(const char*, uint32_t);
using StringNewUtf16Fn = void* (__cdecl*)(const uint16_t*, int32_t);

StringNewFn g_origNew = nullptr;
StringNewFn g_origWrapper = nullptr;
StringNewLenFn g_origNewLen = nullptr;
StringNewUtf16Fn g_origNewUtf16 = nullptr;

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
    if (IpcClient::Instance().RequestTranslation(source, EngineId(GtiEngine::UnityIL2CPP),
                                                 &target, &origin,
                                                 IpcClient::Instance().blockTimeoutMs()) &&
        !target.empty()) {
        IpcClient::Instance().translated.fetch_add(1);
        return target;
    }
    return {};
}

void* StringNewImpl(const char* text) {
    std::string translated = TranslateOrEmpty(text);
    return g_origNew(translated.empty() ? text : translated.c_str());
}

void* StringNewWrapperImpl(const char* text) {
    std::string translated = TranslateOrEmpty(text);
    return g_origWrapper(translated.empty() ? text : translated.c_str());
}

void* StringNewLenImpl(const char* text, uint32_t len) {
    std::string source(text, len);
    std::string translated = TranslateOrEmpty(source);
    return g_origNewLen(translated.empty() ? text : translated.c_str(),
                        static_cast<uint32_t>(translated.size()));
}

void* StringNewUtf16Impl(const uint16_t* text, int32_t len) {
    std::string source = WidenToUtf8(reinterpret_cast<const wchar_t*>(text), len);
    std::string translated = TranslateOrEmpty(source);
    std::wstring wide =
        Utf8ToWiden(translated.empty() ? source.c_str() : translated.c_str());
    return g_origNewUtf16(reinterpret_cast<const uint16_t*>(wide.c_str()),
                          static_cast<int32_t>(wide.size()));
}

void* __cdecl DetourStringNew(const char* text) {
    if (g_inHook || !text) {
        return g_origNew(text);
    }
    g_inHook = true;
    void* result = nullptr;
    __try {
        result = StringNewImpl(text);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log(LogLevel::Warn, "il2cpp string_new hook caught exception");
        result = g_origNew(text);
    }
    g_inHook = false;
    return result;
}

void* __cdecl DetourStringNewWrapper(const char* text) {
    if (g_inHook || !text) {
        return g_origWrapper(text);
    }
    g_inHook = true;
    void* result = nullptr;
    __try {
        result = StringNewWrapperImpl(text);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log(LogLevel::Warn, "il2cpp string_new_wrapper hook caught exception");
        result = g_origWrapper(text);
    }
    g_inHook = false;
    return result;
}

void* __cdecl DetourStringNewLen(const char* text, uint32_t len) {
    if (g_inHook || !text) {
        return g_origNewLen(text, len);
    }
    g_inHook = true;
    void* result = nullptr;
    __try {
        result = StringNewLenImpl(text, len);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log(LogLevel::Warn, "il2cpp string_new_len hook caught exception");
        result = g_origNewLen(text, len);
    }
    g_inHook = false;
    return result;
}

void* __cdecl DetourStringNewUtf16(const uint16_t* text, int32_t len) {
    if (g_inHook || !text || len <= 0) {
        return g_origNewUtf16(text, len);
    }
    g_inHook = true;
    void* result = nullptr;
    __try {
        result = StringNewUtf16Impl(text, len);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log(LogLevel::Warn, "il2cpp string_new_utf16 hook caught exception");
        result = g_origNewUtf16(text, len);
    }
    g_inHook = false;
    return result;
}

}  // namespace

bool InstallUnityIl2CppHooks() {
    HMODULE gameAssembly = GetModuleHandleW(L"GameAssembly.dll");
    if (!gameAssembly) {
        return false;
    }
    Log(LogLevel::Info, "IL2CPP resolution: trying export names");
    void* fnNew = GetModuleExport(gameAssembly, "il2cpp_string_new");
    void* fnWrapper = GetModuleExport(gameAssembly, "il2cpp_string_new_wrapper");
    void* fnNewLen = GetModuleExport(gameAssembly, "il2cpp_string_new_len");
    void* fnNewUtf16 = GetModuleExport(gameAssembly, "il2cpp_string_new_utf16");

    if (!fnNew || !fnWrapper || !fnNewLen || !fnNewUtf16) {
        Log(LogLevel::Info,
            "IL2CPP exports are missing or obfuscated; falling back to signature scan");
        // Pattern table is populated after real-sample analysis of obfuscated builds
        // (e.g. FAPNAF STORYMODE). Keep the framework in place and log honestly.
        std::vector<Pattern> patterns;
        if (!patterns.empty()) {
            if (!fnNew) {
                fnNew = reinterpret_cast<void*>(FindFirstPattern(gameAssembly, patterns));
            }
            if (!fnWrapper) {
                fnWrapper = reinterpret_cast<void*>(FindFirstPattern(gameAssembly, patterns));
            }
        } else {
            Log(LogLevel::Warn,
                "IL2CPP signature table is empty for this build; string hooks skipped");
            return false;
        }
    }

    bool ok = true;
    if (fnNew) {
        ok &= HookManager::Install("il2cpp_string_new", fnNew,
                                   reinterpret_cast<void*>(&DetourStringNew),
                                   reinterpret_cast<void**>(&g_origNew));
    }
    if (fnWrapper) {
        ok &= HookManager::Install("il2cpp_string_new_wrapper", fnWrapper,
                                   reinterpret_cast<void*>(&DetourStringNewWrapper),
                                   reinterpret_cast<void**>(&g_origWrapper));
    }
    if (fnNewLen) {
        ok &= HookManager::Install("il2cpp_string_new_len", fnNewLen,
                                   reinterpret_cast<void*>(&DetourStringNewLen),
                                   reinterpret_cast<void**>(&g_origNewLen));
    }
    if (fnNewUtf16) {
        ok &= HookManager::Install("il2cpp_string_new_utf16", fnNewUtf16,
                                   reinterpret_cast<void*>(&DetourStringNewUtf16),
                                   reinterpret_cast<void**>(&g_origNewUtf16));
    }
    return ok;
}

}  // namespace gti
