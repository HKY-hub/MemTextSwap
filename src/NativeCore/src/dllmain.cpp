#include <atomic>
#include <memory>
#include <string>

#include <windows.h>

#include "gti/engine.h"
#include "gti/gti_version.h"
#include "gti/hooks.h"
#include "gti/ipc_client.h"
#include "gti/log.h"

namespace {

HMODULE g_module = nullptr;
HANDLE g_unloadEvent = nullptr;
HANDLE g_worker = nullptr;
std::atomic<bool> g_detaching{false};

struct RemoteArgs {
    wchar_t pipeName[128];
    uint32_t pid;
    volatile uint32_t phase;
};

}  // namespace

namespace gti {
HMODULE GtiModuleHandle() {
    return g_module;
}

DWORD WINAPI WorkerMain(const std::wstring& pipeName);

DWORD WINAPI WorkerThunk(LPVOID param) {
    std::unique_ptr<std::wstring> pipe(static_cast<std::wstring*>(param));
    return WorkerMain(*pipe);
}

}  // namespace gti

extern "C" {

__declspec(dllexport) BOOL WINAPI GTI_Start(const wchar_t* pipeName, uint32_t targetPid);

namespace gti {
DWORD EntryImpl(RemoteArgs* args, uint32_t* phase) {
    args->phase = 1;
    *phase = 1;
    std::wstring pipeName(args->pipeName);
    args->phase = 2;
    *phase = 2;
    BOOL ok = GTI_Start(pipeName.c_str(), args->pid);
    args->phase = 3;
    *phase = 3;
    return ok ? 0 : 1;
}
}  // namespace gti

__declspec(dllexport) DWORD WINAPI GTI_ThreadEntry(LPVOID arg) {
    auto* args = static_cast<RemoteArgs*>(arg);
    if (!args) {
        return 1;
    }
    uint32_t phase = 0;
    __try {
        return gti::EntryImpl(args, &phase);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0xDEAD0000u | phase;
    }
}

__declspec(dllexport) BOOL WINAPI GTI_Start(const wchar_t* pipeName, uint32_t targetPid) {
    if (!g_unloadEvent) {
        g_unloadEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    }
    bool workerRunning = false;
    if (g_worker) {
        DWORD code = 0;
        if (GetExitCodeThread(g_worker, &code) && code == STILL_ACTIVE) {
            workerRunning = true;
        } else {
            CloseHandle(g_worker);
            g_worker = nullptr;
        }
    }
    if (!workerRunning) {
        auto* pipe = new std::wstring(pipeName ? pipeName : L"");
        g_worker = CreateThread(nullptr, 0, gti::WorkerThunk, pipe, 0, nullptr);
    }
    (void)targetPid;
    return g_worker ? TRUE : FALSE;
}

__declspec(dllexport) void WINAPI GTI_RequestUnload() {
    if (g_unloadEvent) {
        SetEvent(g_unloadEvent);
    }
}

__declspec(dllexport) uint32_t WINAPI GTI_Version() {
    return (GTI_VERSION_MAJOR << 16) | (GTI_VERSION_MINOR << 8) | GTI_VERSION_PATCH;
}

}  // extern "C"

namespace gti {

DWORD WINAPI WorkerMain(const std::wstring& pipeName) {
    SetLogSink([](LogLevel level, const char* message) {
        IpcClient::Instance().SendLogMessage(level, message);
    });
    Log(LogLevel::Info, "NativeCore loaded into pid %lu (%s)", GetCurrentProcessId(),
        sizeof(void*) == 8 ? "x64" : "x86");

    EngineInfo info = DetectEngine();
    Log(LogLevel::Info, "engine detected: %s", info.name.c_str());

    bool hooked = InstallEngineHooks();
    Log(LogLevel::Info, "engine hooks installed: %s", hooked ? "yes" : "no");
    if (info.engine == GtiEngine::UnityIL2CPP) {
        StartUnityScanner();
    }

    IpcClient::Instance().SetUnloadHandler([]() {
        if (g_unloadEvent) {
            SetEvent(g_unloadEvent);
        }
    });

    bool started = IpcClient::Instance().Start(pipeName);
    Log(LogLevel::Info, "IPC client started: %s", started ? "yes" : "no");

    while (WaitForSingleObject(g_unloadEvent, 200) == WAIT_TIMEOUT && !g_detaching.load()) {
    }

    Log(LogLevel::Info, "unloading: disabling hooks, disconnecting IPC");
    StopUnityScanner();
    SetHooksActive(false);
    IpcClient::Instance().Stop();
    HookManager::UninstallAll();
    Log(LogLevel::Info, "waiting for in-flight requests to settle");
    bool idle = IpcClient::Instance().WaitForIdle(3500);
    Log(LogLevel::Info, "in-flight requests idle: %s", idle ? "yes" : "no");
    // The DLL module is intentionally retained: a game thread could still be
    // executing detour code, and FreeLibrary would unmap it mid-execution.
    // Hooks are already restored and IPC stopped, so the game is fully
    // functional; the dormant module is a few KB and is reused on re-inject.
    Log(LogLevel::Info, "unload complete (module retained for safety)");
    (void)idle;
    return 0;
}

}  // namespace gti

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = instance;
        DisableThreadLibraryCalls(instance);
    } else if (reason == DLL_PROCESS_DETACH) {
        g_detaching.store(true);
        if (g_unloadEvent) {
            SetEvent(g_unloadEvent);
        }
        // Best-effort cleanup when the process exits while hooks are installed.
        gti::HookManager::UninstallAll();
        gti::IpcClient::Instance().Stop();
        if (g_worker) {
            CloseHandle(g_worker);
            g_worker = nullptr;
        }
    }
    return TRUE;
}
