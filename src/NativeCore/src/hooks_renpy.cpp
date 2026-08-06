#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwctype>
#include <string>

#include <windows.h>

#include "gti/engine.h"
#include "gti/hooks.h"
#include "gti/ipc_client.h"
#include "gti/log.h"

namespace gti {
namespace {

using PyGILState_STATE = unsigned long;
using PyGILStateEnsureFn = PyGILState_STATE(__cdecl*)();
using PyGILStateReleaseFn = void(__cdecl*)(PyGILState_STATE);
using PyRunSimpleStringFn = int(__cdecl*)(const char*);

PyGILStateEnsureFn g_pyEnsure = nullptr;
PyGILStateReleaseFn g_pyRelease = nullptr;
PyRunSimpleStringFn g_pyRun = nullptr;

std::atomic<bool> g_patched{false};

HMODULE FindPythonModule() {
    std::vector<std::wstring> names;
    if (!EnumModuleNames(&names)) {
        return nullptr;
    }
    HMODULE fallback = nullptr;
    for (const auto& name : names) {
        std::wstring lower = name;
        for (auto& c : lower) {
            c = static_cast<wchar_t>(towlower(c));
        }
        if (lower.find(L"python") != std::wstring::npos &&
            lower.find(L".dll") != std::wstring::npos) {
            HMODULE mod = GetModuleHandleW(name.c_str());
            if (mod) {
                Log(LogLevel::Info, "renpy python module candidate: %ls", name.c_str());
                bool runtime = lower.rfind(L"python", 0) == 0 ||
                               lower.rfind(L"libpython", 0) == 0;
                if (runtime) {
                    return mod;
                }
                if (!fallback) {
                    fallback = mod;
                }
            }
        }
    }
    return fallback;
}

std::wstring GtiDllPath() {
    wchar_t buffer[MAX_PATH * 4] = {};
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&GtiDllPath), &self);
    if (!self) {
        return L"";
    }
    DWORD size = GetModuleFileNameW(self, buffer, MAX_PATH * 4);
    return std::wstring(buffer, size);
}

}  // namespace

// Called by the injected Python bootstrap through ctypes.
extern "C" __declspec(dllexport) void* __stdcall gti_py_translate(const char* utf8) {
    if (!HooksActive() || !utf8) {
        return nullptr;
    }
    std::string target;
    std::string origin;
    if (IpcClient::Instance().RequestTranslation(
            utf8, EngineId(GtiEngine::RenPy), &target, &origin,
            IpcClient::Instance().blockTimeoutMs()) &&
        !target.empty()) {
        void* buffer = std::malloc(target.size() + 1);
        if (!buffer) {
            return nullptr;
        }
        std::memcpy(buffer, target.c_str(), target.size() + 1);
        return buffer;
    }
    return nullptr;
}

extern "C" __declspec(dllexport) void __stdcall gti_py_translate_free(void* ptr) {
    std::free(ptr);
}

bool InstallRenPyHooks() {
    if (g_patched.load()) {
        return true;
    }
    HMODULE python = FindPythonModule();
    if (!python) {
        Log(LogLevel::Warn, "RenPy python module not found");
        return false;
    }
    g_pyEnsure = reinterpret_cast<PyGILStateEnsureFn>(GetProcAddress(python, "PyGILState_Ensure"));
    g_pyRelease =
        reinterpret_cast<PyGILStateReleaseFn>(GetProcAddress(python, "PyGILState_Release"));
    g_pyRun = reinterpret_cast<PyRunSimpleStringFn>(GetProcAddress(python, "PyRun_SimpleString"));
    if (!g_pyEnsure || !g_pyRelease || !g_pyRun) {
        Log(LogLevel::Warn, "python API symbols not found (PyGILState_* / PyRun_SimpleString)");
        return false;
    }

    std::wstring dllPath = GtiDllPath();
    std::string dllPathUtf8;
    for (wchar_t c : dllPath) {
        if (c == L'\\') {
            dllPathUtf8.push_back('/');
        } else {
            char out[4] = {};
            int n = WideCharToMultiByte(CP_UTF8, 0, &c, 1, out, sizeof(out), nullptr, nullptr);
            dllPathUtf8.append(out, static_cast<size_t>(n));
        }
    }
    std::string dllPathEscaped;
    for (char c : dllPathUtf8) {
        if (c == '\'') {
            dllPathEscaped += "\\'";
        } else {
            dllPathEscaped.push_back(c);
        }
    }
    char markerPath[MAX_PATH] = {};
    GetTempPathA(MAX_PATH, markerPath);
    strcat_s(markerPath, "gti-renpy-bootstrap.log");
    std::string markerEscaped;
    for (char c : std::string(markerPath)) {
        if (c == '\'') {
            markerEscaped += "\\'";
        } else {
            markerEscaped.push_back(c);
        }
    }

    std::string script = R"PY(
import sys
_gti_marker = r'@@MARKER@@'
def _gti_fail(e):
    try:
        open(_gti_marker, 'w').write('ERR: ' + repr(e))
    except Exception:
        pass
try:
    import ctypes
    _gti_dll = None
    try:
        _gti_dll = ctypes.WinDLL(r'@@DLL@@')
        _gti_dll.gti_py_translate.restype = ctypes.c_void_p
        _gti_dll.gti_py_translate.argtypes = [ctypes.c_char_p]
        _gti_dll.gti_py_translate_free.argtypes = [ctypes.c_void_p]
    except Exception as e:
        _gti_fail(e)
        _gti_dll = None

    def gti_translate(s):
        if _gti_dll is None or not isinstance(s, str):
            return None
        try:
            p = _gti_dll.gti_py_translate(s.encode('utf-8'))
            if not p:
                return None
            try:
                return ctypes.string_at(p).decode('utf-8')
            finally:
                _gti_dll.gti_py_translate_free(p)
        except Exception:
            return None

    def _gti_patch():
        try:
            import renpy.translation
            import renpy.text.text
            _orig = renpy.translation.translate_string
            def translate_string(s, interact=True):
                r = gti_translate(s)
                return r if r is not None else _orig(s, interact)
            renpy.translation.translate_string = translate_string
            _orig_init = renpy.text.text.Text.__init__
            def _init(self, text, *a, **kw):
                if isinstance(text, str):
                    r = gti_translate(text)
                    if r is not None:
                        text = r
                return _orig_init(self, text, *a, **kw)
            renpy.text.text.Text.__init__ = _init
        except Exception as e:
            _gti_fail(e)

    _gti_patch()
    open(_gti_marker, 'w').write('OK')
except BaseException as e:
    _gti_fail(e)
)PY";
    auto ReplaceAll = [](std::string& text, const std::string& from,
                         const std::string& to) {
        size_t pos = 0;
        while ((pos = text.find(from, pos)) != std::string::npos) {
            text.replace(pos, from.size(), to);
            pos += to.size();
        }
    };
    ReplaceAll(script, "@@MARKER@@", markerEscaped);
    ReplaceAll(script, "@@DLL@@", dllPathEscaped);
    PyGILState_STATE state = g_pyEnsure();
    int rc = g_pyRun(script.c_str());
    g_pyRelease(state);
    g_patched.store(rc == 0);
    std::string marker;
    if (FILE* f = fopen(markerPath, "rb")) {
        char buffer[512] = {};
        size_t n = fread(buffer, 1, sizeof(buffer) - 1, f);
        buffer[n] = '\0';
        marker = buffer;
        fclose(f);
    }
    Log(LogLevel::Info, "RenPy python patch applied: %s (marker=%s)",
        g_patched.load() ? "yes" : "no", marker.c_str());
    return g_patched.load();
}

}  // namespace gti
