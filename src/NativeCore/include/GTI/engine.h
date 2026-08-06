#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <windows.h>

namespace gti {

enum class GtiEngine : uint32_t {
    Unknown = 0,
    TestTarget = 1,
    UnityIL2CPP = 2,
    UnityMono = 3,
    RenPy = 4,
    RpgMakerMv = 5,
    RpgMakerMz = 6,
};

const char* EngineName(GtiEngine engine);
uint32_t EngineId(GtiEngine engine);

struct EngineInfo {
    GtiEngine engine = GtiEngine::Unknown;
    std::string name;
};

// In-process engine detection based on loaded modules and exports.
EngineInfo DetectEngine();

// Enumerate module file names currently loaded in this process.
bool EnumModuleNames(std::vector<std::wstring>* names);
void* GetModuleExport(HMODULE mod, const char* name);

}  // namespace gti
