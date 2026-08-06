#include <string>

#include <windows.h>

#include "gti/engine.h"
#include "gti/hooks.h"
#include "gti/log.h"
#include "gti/signature_scan.h"

namespace gti {
namespace {

// Primary path: hook v8::Script::Run once to evaluate a JS bootstrap that wraps
// Bitmap.prototype.drawText / Window_Base.prototype.drawTextEx.
// v8 symbols are not exported by NW.js release builds, so resolution uses a
// signature table. The table is populated from real-sample analysis (Phase 3);
// an empty table logs the limitation instead of risking a false positive hook.
const std::vector<Pattern>& ScriptRunPatterns() {
    static const std::vector<Pattern> patterns = {
        // Placeholder: add verified byte patterns per NW.js version here.
        // Example format: {"48 89 5C 24 ?? 57 48 83 EC ?? 48 8B 05 ?? ?? ?? ??"}
    };
    return patterns;
}

}  // namespace

bool InstallRpgMakerHooks() {
    HMODULE nw = GetModuleHandleW(L"nw.dll");
    if (!nw) {
        nw = GetModuleHandleW(L"nwjs.dll");
    }
    if (!nw) {
        Log(LogLevel::Warn, "nw.dll not loaded; RPGMaker hook skipped");
        return false;
    }
    const auto& patterns = ScriptRunPatterns();
    if (patterns.empty()) {
        Log(LogLevel::Warn,
            "RPGMaker v8 signature table is empty; JS bootstrap hook deferred to "
            "real-sample analysis. Engine detected and IPC pipeline remain active.");
        return false;
    }
    uint8_t* scriptRun = FindFirstPattern(nw, patterns);
    if (!scriptRun) {
        Log(LogLevel::Warn, "v8::Script::Run signature not found");
        return false;
    }
    Log(LogLevel::Info, "v8::Script::Run found at %p", scriptRun);
    // TODO(phase3): install MinHook on scriptRun and evaluate the bootstrap once
    // a verified detour prototype exists. Deliberately not installing an unverified
    // hook so real games are never crashed by a guessed signature.
    return false;
}

}  // namespace gti
