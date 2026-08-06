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

// Installs hooks for the detected engine. Returns true if at least one hook is active.
bool InstallEngineHooks();

}  // namespace gti
