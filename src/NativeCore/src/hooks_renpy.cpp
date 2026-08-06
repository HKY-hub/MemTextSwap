#include <atomic>
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
    for (const auto& name : names) {
        std::wstring lower = name;
        for (auto& c : lower) {
            c = static_cast<wchar_t>(towlower(c));
        }
        if (lower.rfind(L"python", 0) == 0 && lower.find(L".dll") != std::wstring::npos) {
            HMODULE mod = GetModuleHandleW(name.c_str());
            if (mod) {
                return mod;
            }
        }
    }
    return nullptr;
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
    if (!utf8) {
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

    std::string script =
        "import ctypes\n"
        "_gti_dll = None\n"
        "try:\n"
        "    _gti_dll = ctypes.WinDLL(r'" +
        dllPathUtf8 +
        "')\n"
        "    _gti_dll.gti_py_translate.restype = ctypes.c_void_p\n"
        "    _gti_dll.gti_py_translate.argtypes = [ctypes.c_char_p]\n"
        "    _gti_dll.gti_py_translate_free.argtypes = [ctypes.c_void_p]\n"
        "except Exception:\n"
        "    _gti_dll = None\n"
        "\n"
        "def gti_translate(s):\n"
        "    if _gti_dll is None or not isinstance(s, str):\n"
        "        return None\n"
        "    try:\n"
        "        p = _gti_dll.gti_py_translate(s.encode('utf-8'))\n"
        "        if not p:\n"
        "            return None\n"
        "        try:\n"
        "            return ctypes.string_at(p).decode('utf-8')\n"
        "        finally:\n"
        "            _gti_dll.gti_py_translate_free(p)\n"
        "    except Exception:\n"
        "        return None\n"
        "\n"
        "def _gti_patch():\n"
        "    try:\n"
        "        import renpy.translation\n"
        "        import renpy.text.text\n"
        "        _orig = renpy.translation.translate_string\n"
        "        def translate_string(s, interact=True):\n"
        "            r = gti_translate(s)\n"
        "            return r if r is not None else _orig(s, interact)\n"
        "        renpy.translation.translate_string = translate_string\n"
        "        _orig_init = renpy.text.text.Text.__init__\n"
        "        def _init(self, text, *a, **kw):\n"
        "            if isinstance(text, str):\n"
        "                r = gti_translate(text)\n"
        "                if r is not None:\n"
        "                    text = r\n"
        "            return _orig_init(self, text, *a, **kw)\n"
        "        renpy.text.text.Text.__init__ = _init\n"
        "    except Exception:\n"
        "        pass\n"
        "\n"
        "_gti_patch()\n";

    PyGILState_STATE state = g_pyEnsure();
    int rc = g_pyRun(script.c_str());
    g_pyRelease(state);
    g_patched.store(rc == 0);
    Log(LogLevel::Info, "RenPy python patch applied: %s", g_patched.load() ? "yes" : "no");
    return g_patched.load();
}

}  // namespace gti
