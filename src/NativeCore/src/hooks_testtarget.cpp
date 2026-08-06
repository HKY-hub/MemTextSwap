#include <atomic>
#include <cstdlib>
#include <string>

#include <windows.h>

#include "gti/engine.h"
#include "gti/hooks.h"
#include "gti/ipc_client.h"
#include "gti/log.h"
#include "gti/text_filter.h"

namespace gti {
namespace {

using ProduceLineFn = const char* (__cdecl*)(int*);
ProduceLineFn g_origProduceLine = nullptr;
thread_local bool g_lastApplied = false;

const char* ProduceLineImpl(int* counter) {
    g_lastApplied = false;
    const char* result = g_origProduceLine(counter);
    std::string reason;
    if (result && gTextFilter.ShouldTranslate(result, &reason)) {
        IpcClient::Instance().captured.fetch_add(1);
        std::string target;
        std::string origin;
        if (IpcClient::Instance().RequestTranslation(
                result, EngineId(GtiEngine::TestTarget), &target, &origin,
                IpcClient::Instance().blockTimeoutMs()) &&
            !target.empty()) {
            thread_local std::string translated;
            translated = target;
            result = translated.c_str();
            IpcClient::Instance().translated.fetch_add(1);
            g_lastApplied = true;
        }
    } else if (result) {
        IpcClient::Instance().skipped.fetch_add(1);
    }
    return result;
}

const char* __cdecl DetourProduceLine(int* counter) {
    if (!HooksActive()) {
        return g_origProduceLine(counter);
    }
    thread_local bool inHook = false;
    if (inHook) {
        return g_origProduceLine(counter);
    }
    inHook = true;
    const char* result = nullptr;
    __try {
        result = ProduceLineImpl(counter);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log(LogLevel::Warn, "testtarget hook caught exception");
        result = g_origProduceLine(counter);
    }
    inHook = false;
    return result;
}

}  // namespace

bool InstallTestTargetHooks() {
    if (std::getenv("GTI_DISABLE_TEST_HOOK")) {
        Log(LogLevel::Warn, "testtarget hook disabled by GTI_DISABLE_TEST_HOOK");
        return false;
    }
    HMODULE exe = GetModuleHandleW(nullptr);
    void* target = GetModuleExport(exe, "GTI_ProduceLine");
    if (!target) {
        Log(LogLevel::Warn, "testtarget export not found");
        return false;
    }
    return HookManager::Install("testtarget.GTI_ProduceLine", target,
                                reinterpret_cast<void*>(&DetourProduceLine),
                                reinterpret_cast<void**>(&g_origProduceLine));
}

}  // namespace gti
