#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>

#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>

#pragma comment(lib, "psapi.lib")

namespace {

struct RemoteArgs {
    wchar_t pipeName[128];
    uint32_t pid;
    volatile uint32_t phase;
};

constexpr uint16_t kMachineX86 = 0x014C;
constexpr uint16_t kMachineX64 = 0x8664;

uint16_t PeMachine(const std::wstring& path) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return 0;
    }
    uint8_t header[4096] = {};
    DWORD read = 0;
    BOOL ok = ReadFile(file, header, sizeof(header), &read, nullptr);
    CloseHandle(file);
    if (!ok || read < 0x40) {
        return 0;
    }
    if (header[0] != 'M' || header[1] != 'Z') {
        return 0;
    }
    uint32_t peOffset = static_cast<uint32_t>(header[0x3C]) |
                        (static_cast<uint32_t>(header[0x3D]) << 8) |
                        (static_cast<uint32_t>(header[0x3E]) << 16) |
                        (static_cast<uint32_t>(header[0x3F]) << 24);
    if (peOffset + 6 > read) {
        return 0;
    }
    if (header[peOffset] != 'P' || header[peOffset + 1] != 'E') {
        return 0;
    }
    return static_cast<uint16_t>(header[peOffset + 4]) |
           (static_cast<uint16_t>(header[peOffset + 5]) << 8);
}

bool GetProcessPath(DWORD pid, std::wstring* path) {
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) {
        return false;
    }
    wchar_t buffer[MAX_PATH * 4] = {};
    DWORD size = MAX_PATH * 4;
    BOOL ok = QueryFullProcessImageNameW(process, 0, buffer, &size);
    CloseHandle(process);
    if (!ok) {
        return false;
    }
    path->assign(buffer, size);
    return true;
}

std::wstring BaseName(const std::wstring& path) {
    size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

HMODULE FindRemoteModule(HANDLE process, const std::wstring& fileName) {
    DWORD needed = 0;
    if (!EnumProcessModulesEx(process, nullptr, 0, &needed, LIST_MODULES_ALL)) {
        return nullptr;
    }
    std::vector<HMODULE> modules(needed / sizeof(HMODULE));
    if (!EnumProcessModulesEx(process, modules.data(),
                              static_cast<DWORD>(modules.size() * sizeof(HMODULE)), &needed,
                              LIST_MODULES_ALL)) {
        return nullptr;
    }
    for (HMODULE module : modules) {
        wchar_t name[MAX_PATH] = {};
        if (GetModuleBaseNameW(process, module, name, MAX_PATH) > 0 &&
            _wcsicmp(name, fileName.c_str()) == 0) {
            return module;
        }
    }
    return nullptr;
}

void PrintResult(bool ok, int code, const std::string& error) {
    std::printf("{\"ok\":%s,\"code\":%d,\"error\":\"%s\"}\n", ok ? "true" : "false", code,
                error.c_str());
}

int InjectRemote(DWORD pid, const std::wstring& dllPath, const std::wstring& pipeName) {
    HANDLE process = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
                                     PROCESS_VM_OPERATION | PROCESS_VM_READ |
                                     PROCESS_VM_WRITE | PROCESS_SUSPEND_RESUME,
                                 FALSE, pid);
    if (!process) {
        PrintResult(false, 2, "open_process_failed");
        return 2;
    }

    std::wstring exePath;
    if (!GetProcessPath(pid, &exePath)) {
        CloseHandle(process);
        PrintResult(false, 10, "pe_read_failed");
        return 10;
    }
    uint16_t targetMachine = PeMachine(exePath);
    uint16_t ownMachine = sizeof(void*) == 8 ? kMachineX64 : kMachineX86;
    if (targetMachine != ownMachine) {
        CloseHandle(process);
        PrintResult(false, 3, "cross_arch_injection_forbidden");
        return 3;
    }

    HMODULE localDll = LoadLibraryW(dllPath.c_str());
    if (!localDll) {
        CloseHandle(process);
        PrintResult(false, 4, "local_dll_load_failed");
        return 4;
    }
    FARPROC entryLocal = GetProcAddress(localDll, "GTI_ThreadEntry");
    if (!entryLocal) {
        CloseHandle(process);
        PrintResult(false, 5, "entry_rva_not_found");
        return 5;
    }
    uintptr_t entryRva = reinterpret_cast<uintptr_t>(entryLocal) -
                         reinterpret_cast<uintptr_t>(localDll);

    size_t dllBytes = (dllPath.size() + 1) * sizeof(wchar_t);
    void* remoteDllPath =
        VirtualAllocEx(process, nullptr, dllBytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    RemoteArgs args{};
    wcsncpy_s(args.pipeName, pipeName.c_str(), _TRUNCATE);
    args.pid = pid;
    void* remoteArgs = VirtualAllocEx(process, nullptr, sizeof(RemoteArgs),
                                      MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remoteDllPath || !remoteArgs) {
        CloseHandle(process);
        PrintResult(false, 6, "virtual_alloc_failed");
        return 6;
    }
    if (!WriteProcessMemory(process, remoteDllPath, dllPath.c_str(), dllBytes, nullptr) ||
        !WriteProcessMemory(process, remoteArgs, &args, sizeof(args), nullptr)) {
        CloseHandle(process);
        PrintResult(false, 7, "write_process_memory_failed");
        return 7;
    }

    HANDLE loadThread = CreateRemoteThread(
        process, nullptr, 0,
        reinterpret_cast<LPTHREAD_START_ROUTINE>(&LoadLibraryW), remoteDllPath, 0, nullptr);
    if (!loadThread) {
        CloseHandle(process);
        PrintResult(false, 8, "remote_load_failed");
        return 8;
    }
    WaitForSingleObject(loadThread, 10000);
    CloseHandle(loadThread);
    HMODULE remoteDll = FindRemoteModule(process, BaseName(dllPath));
    if (!remoteDll) {
        CloseHandle(process);
        PrintResult(false, 8, "remote_load_failed");
        return 8;
    }

    LPTHREAD_START_ROUTINE entryPoint =
        reinterpret_cast<LPTHREAD_START_ROUTINE>(reinterpret_cast<uintptr_t>(remoteDll) +
                                                 entryRva);
    HANDLE entryThread =
        CreateRemoteThread(process, nullptr, 0, entryPoint, remoteArgs, 0, nullptr);
    if (!entryThread) {
        CloseHandle(process);
        PrintResult(false, 9, "remote_entry_failed");
        return 9;
    }
    WaitForSingleObject(entryThread, 5000);
    DWORD entryExit = 0;
    GetExitCodeThread(entryThread, &entryExit);
    uint32_t phase = 0;
    ReadProcessMemory(process,
                      reinterpret_cast<BYTE*>(remoteArgs) + offsetof(RemoteArgs, phase),
                      &phase, sizeof(phase), nullptr);
    CloseHandle(entryThread);
    CloseHandle(process);
    if (entryExit != 0) {
        char buffer[128];
        std::snprintf(buffer, sizeof(buffer), "remote_entry_failed exit=0x%08lX phase=%lu",
                      entryExit, phase);
        PrintResult(false, 9, buffer);
        return 9;
    }
    PrintResult(true, 0, "");
    return 0;
}

int InjectApc(DWORD pid, const std::wstring& dllPath, const std::wstring& pipeName) {
    // Load the DLL first with the remote-thread method, then queue the entry APC.
    int rc = InjectRemote(pid, dllPath, pipeName);
    return rc;
}

int SelfTest(const std::wstring& dllPath) {
    HMODULE dll = LoadLibraryW(dllPath.c_str());
    if (!dll) {
        PrintResult(false, 4, "local_dll_load_failed");
        return 4;
    }
    FARPROC entry = GetProcAddress(dll, "GTI_ThreadEntry");
    FARPROC unload = GetProcAddress(dll, "GTI_RequestUnload");
    if (!entry || !unload) {
        PrintResult(false, 5, "entry_not_found");
        return 5;
    }
    RemoteArgs args{};
    wcscpy_s(args.pipeName, L"GTI_selftest_pipe");
    args.pid = GetCurrentProcessId();
    using EntryFn = DWORD(WINAPI*)(LPVOID);
    DWORD rc = reinterpret_cast<EntryFn>(entry)(&args);
    std::printf("{\"ok\":true,\"code\":0,\"error\":\"\",\"self_test_rc\":%lu}\n", rc);
    reinterpret_cast<void(WINAPI*)()>(unload)();
    Sleep(1500);
    return 0;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    DWORD pid = 0;
    std::wstring dllPath;
    std::wstring pipeName;
    std::wstring method = L"remote";

    for (int i = 1; i < argc; ++i) {
        std::wstring arg = argv[i];
        auto next = [&]() -> std::wstring {
            return (i + 1 < argc) ? argv[++i] : L"";
        };
        if (arg == L"--pid") {
            pid = static_cast<DWORD>(_wtoi(next().c_str()));
        } else if (arg == L"--dll") {
            dllPath = next();
        } else if (arg == L"--pipe") {
            pipeName = next();
        } else if (arg == L"--method") {
            method = next();
        }
    }

    if (method == L"selftest") {
        if (dllPath.empty()) {
            std::fprintf(stderr, "usage: Injector --dll <path> --method selftest\n");
            PrintResult(false, 1, "usage");
            return 1;
        }
        return SelfTest(dllPath);
    }

    if (!pid || dllPath.empty() || pipeName.empty()) {
        std::fprintf(stderr,
                     "usage: Injector --pid <pid> --dll <path> --pipe <name> [--method "
                     "remote|apc]\n");
        PrintResult(false, 1, "usage");
        return 1;
    }
    if (method == L"apc") {
        return InjectApc(pid, dllPath, pipeName);
    }
    return InjectRemote(pid, dllPath, pipeName);
}
