#include "gti/hooks.h"

#include <algorithm>
#include <atomic>
#include <mutex>

#include <MinHook.h>
#include <windows.h>

#include "gti/engine.h"
#include "gti/log.h"

namespace gti {

namespace {
std::atomic<bool> g_hooksActive{true};
}

bool HooksActive() {
    return g_hooksActive.load();
}

void SetHooksActive(bool active) {
    g_hooksActive.store(active);
}

// Forward declarations of per-engine installers (one per hooks_*.cpp).
bool InstallTestTargetHooks();
bool InstallUnityIl2CppHooks();
bool InstallUnityMonoHooks();
bool InstallRenPyHooks();
bool InstallRpgMakerHooks();

bool InstallEngineHooks() {
    EngineInfo info = DetectEngine();
    switch (info.engine) {
        case GtiEngine::TestTarget:
            return InstallTestTargetHooks();
        case GtiEngine::UnityIL2CPP:
            return InstallUnityIl2CppHooks();
        case GtiEngine::UnityMono:
            return InstallUnityMonoHooks();
        case GtiEngine::RenPy:
            return InstallRenPyHooks();
        case GtiEngine::RpgMakerMv:
        case GtiEngine::RpgMakerMz:
            return InstallRpgMakerHooks();
        default:
            Log(LogLevel::Warn, "no hooks for unknown engine");
            return false;
    }
}

std::vector<HookManager::Entry>& HookManager::entries() {
    static std::vector<Entry> instance;
    return instance;
}

bool HookManager::Install(const char* name, void* target, void* detour, void** original) {
    if (!target || !detour) {
        Log(LogLevel::Error, "hook '%s' skipped: invalid target/detour", name ? name : "?");
        return false;
    }
    static std::mutex mutex;
    std::lock_guard<std::mutex> lock(mutex);
    if (MH_Initialize() != MH_OK) {
        // MH_Initialize fails on second call; that is fine if already initialized.
    }
    void* trampoline = nullptr;
    MH_STATUS status = MH_CreateHook(target, detour, &trampoline);
    if (status != MH_OK) {
        if (status == MH_ERROR_ALREADY_CREATED) {
            // The target is already hooked (e.g. two exports alias the same
            // address); treat as a no-op rather than an error.
            Log(LogLevel::Info, "hook '%s' already created (alias target)", name ? name : "?");
            return true;
        }
        Log(LogLevel::Warn, "hook '%s' create failed: %d", name ? name : "?", status);
        return false;
    }
    status = MH_EnableHook(target);
    if (status != MH_OK) {
        Log(LogLevel::Warn, "hook '%s' enable failed: %d", name ? name : "?", status);
        return false;
    }
    if (original) {
        *original = trampoline;
    }
    entries().push_back({name ? name : "?", target, detour, trampoline});
    Log(LogLevel::Info, "hook installed: %s", name ? name : "?");
    return true;
}

bool HookManager::UninstallAll() {
    bool ok = true;
    for (auto& entry : entries()) {
        if (MH_DisableHook(entry.target) != MH_OK) {
            ok = false;
        }
    }
    FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
    // MH_RemoveHook / MH_Uninitialize are intentionally NOT called: a game
    // thread may still be executing the trampoline, and freeing it would
    // unmap code that is currently running. Disabled hooks + retained
    // trampolines fully restore the game and are safe for re-injection.
    Log(LogLevel::Info, "hooks disabled (%s), trampolines retained for safety",
        ok ? "ok" : "partial");
    return ok;
}

bool HookManager::installed() {
    return !entries().empty();
}

}  // namespace gti
