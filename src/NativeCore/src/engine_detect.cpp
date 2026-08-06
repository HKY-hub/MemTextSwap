#include "gti/engine.h"

#include <cwchar>

#include <windows.h>
#include <tlhelp32.h>

namespace gti {
namespace {

bool EndsWithIgnoreCase(const std::wstring& value, const std::wstring& suffix) {
    if (value.size() < suffix.size()) {
        return false;
    }
    return _wcsicmp(value.c_str() + (value.size() - suffix.size()), suffix.c_str()) == 0;
}

bool HasModule(const std::wstring& name) {
    HMODULE mod = GetModuleHandleW(name.c_str());
    return mod != nullptr;
}

bool HasPythonModule(const std::vector<std::wstring>& names) {
    for (const auto& n : names) {
        std::wstring lower = n;
        for (auto& c : lower) {
            c = static_cast<wchar_t>(towlower(c));
        }
        if (lower.find(L"python") != std::wstring::npos &&
            lower.find(L".dll") != std::wstring::npos) {
            return true;
        }
    }
    return false;
}

}  // namespace

const char* EngineName(GtiEngine engine) {
    switch (engine) {
        case GtiEngine::TestTarget:
            return "TestTarget";
        case GtiEngine::UnityIL2CPP:
            return "Unity IL2CPP";
        case GtiEngine::UnityMono:
            return "Unity Mono";
        case GtiEngine::RenPy:
            return "RenPy";
        case GtiEngine::RpgMakerMv:
            return "RPGMaker MV";
        case GtiEngine::RpgMakerMz:
            return "RPGMaker MZ";
        default:
            return "Unknown";
    }
}

uint32_t EngineId(GtiEngine engine) {
    return static_cast<uint32_t>(engine);
}

void* GetModuleExport(HMODULE mod, const char* name) {
    if (!mod) {
        return nullptr;
    }
    return reinterpret_cast<void*>(GetProcAddress(mod, name));
}

bool EnumModuleNames(std::vector<std::wstring>* names) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32,
                                           GetCurrentProcessId());
    if (snap == INVALID_HANDLE_VALUE) {
        return false;
    }
    MODULEENTRY32W me{};
    me.dwSize = sizeof(me);
    if (Module32FirstW(snap, &me)) {
        do {
            names->emplace_back(me.szModule);
        } while (Module32NextW(snap, &me));
    }
    CloseHandle(snap);
    return !names->empty();
}

EngineInfo DetectEngine() {
    std::vector<std::wstring> modules;
    EnumModuleNames(&modules);

    // Unity IL2CPP
    if (HasModule(L"GameAssembly.dll") && HasModule(L"UnityPlayer.dll")) {
        return {GtiEngine::UnityIL2CPP, "Unity IL2CPP"};
    }
    // Unity Mono
    if (HasModule(L"UnityPlayer.dll") &&
        (HasModule(L"mono-2.0-bdwgc.dll") || HasModule(L"mono.dll"))) {
        return {GtiEngine::UnityMono, "Unity Mono"};
    }
    // RPGMaker MV / MZ
    if (HasModule(L"nw.dll") || HasModule(L"nwjs.dll")) {
        return {GtiEngine::RpgMakerMz, "RPGMaker (NW.js)"};
    }
    // RenPy embeds a python runtime
    if (HasPythonModule(modules)) {
        return {GtiEngine::RenPy, "RenPy"};
    }
    // TestTarget harness
    if (GetModuleExport(GetModuleHandleW(nullptr), "GTI_ProduceLine") != nullptr) {
        return {GtiEngine::TestTarget, "TestTarget"};
    }
    return {GtiEngine::Unknown, "Unknown"};
}

}  // namespace gti
