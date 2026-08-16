#pragma once

#include <string>
#include <vector>

namespace gti {

class HookManager {
public:
    static bool Install(const char* name, void* target, void* detour, void** original);
    static bool UninstallAll();
    static bool installed();

private:
    struct Entry {
        std::string name;
        void* target = nullptr;
        void* detour = nullptr;
        void* original = nullptr;
    };
    static std::vector<Entry>& entries();
};

// Global switch used during unload: detours check it first and fall straight
// through to the original when translation is being torn down.
bool HooksActive();
void SetHooksActive(bool active);

// Installs hooks for the detected engine. Returns true if at least one hook is active.
bool InstallEngineHooks();

// Unity IL2CPP static scanner: periodically enumerates existing Text/TMP_Text
// components, translates their current text and writes it back via set_text.
// This covers text that was created before hook installation.
void StartUnityScanner();
void StopUnityScanner();

}  // namespace gti
