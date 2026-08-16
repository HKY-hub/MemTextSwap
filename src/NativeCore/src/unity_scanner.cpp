#include "gti/hooks.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include <windows.h>

#include "gti/engine.h"
#include "gti/ipc_client.h"
#include "gti/log.h"
#include "gti/string_util.h"
#include "gti/text_filter.h"

namespace gti {
namespace {

// ---- il2cpp API（通过 GameAssembly.dll 导出解析） ----

using RuntimeInvokeFn = void* (*)(const void* method, void* obj, void** params, void** exc);
using ClassFromNameFn = void* (*)(void* image, const char* ns, const char* name);
using ImageFromAssemblyFn = void* (*)(void* assembly);
using DomainGetAssembliesFn = void* (*)(void* domain, size_t* size);
using ClassGetMethodFn = void* (*)(void* klass, const char* name, int parameterCount);
using ClassGetTypeFn = void* (*)(void* klass);
using TypeGetObjectFn = void* (*)(void* type);
using ArrayLengthFn = size_t (*)(void* array);
using StringCharsFn = void* (*)(void* str);
using StringLengthFn = int32_t (*)(void* str);
using StringNewWrapperFn = void* (*)(const char* str);

struct Api {
    RuntimeInvokeFn runtimeInvoke = nullptr;
    ClassFromNameFn classFromName = nullptr;
    ImageFromAssemblyFn imageFromAssembly = nullptr;
    DomainGetAssembliesFn domainGetAssemblies = nullptr;
    ClassGetMethodFn classGetMethod = nullptr;
    ClassGetTypeFn classGetType = nullptr;
    TypeGetObjectFn typeGetObject = nullptr;
    ArrayLengthFn arrayLength = nullptr;
    StringCharsFn stringChars = nullptr;
    StringLengthFn stringLength = nullptr;
    StringNewWrapperFn stringNewWrapper = nullptr;
    bool ok = false;
};

Api g_api;
std::atomic<bool> g_resolveDone{false};
std::atomic<bool> g_scannerRunning{false};
std::atomic<bool> g_scanRequested{false};
std::atomic<bool> g_scanInProgress{false};
HANDLE g_scannerThread = nullptr;

struct TextTarget {
    void* klass = nullptr;
    void* getText = nullptr;
    void* setText = nullptr;
    std::string label;
};

std::vector<TextTarget> g_targets;
void* g_objectClass = nullptr;
void* g_findMethod = nullptr;

// object -> { source hash, 下次允许时间(steady ms) }
std::mutex g_seenMutex;
std::map<void*, std::pair<uint64_t, uint64_t>> g_seen;
constexpr size_t kSeenCapacity = 4096;

void ScanOnePass();

void* GetProc(const char* name) {
    HMODULE gameAssembly = GetModuleHandleW(L"GameAssembly.dll");
    if (!gameAssembly) {
        return nullptr;
    }
    return reinterpret_cast<void*>(GetProcAddress(gameAssembly, name));
}

bool ResolveApi() {
    if (g_resolveDone.load()) {
        return g_api.ok;
    }
    g_resolveDone.store(true);
    g_api.runtimeInvoke = reinterpret_cast<RuntimeInvokeFn>(GetProc("il2cpp_runtime_invoke"));
    g_api.classFromName = reinterpret_cast<ClassFromNameFn>(GetProc("il2cpp_class_from_name"));
    g_api.imageFromAssembly =
        reinterpret_cast<ImageFromAssemblyFn>(GetProc("il2cpp_assembly_get_image"));
    g_api.domainGetAssemblies =
        reinterpret_cast<DomainGetAssembliesFn>(GetProc("il2cpp_domain_get_assemblies"));
    g_api.classGetMethod =
        reinterpret_cast<ClassGetMethodFn>(GetProc("il2cpp_class_get_method_from_name"));
    g_api.classGetType = reinterpret_cast<ClassGetTypeFn>(GetProc("il2cpp_class_get_type"));
    g_api.typeGetObject = reinterpret_cast<TypeGetObjectFn>(GetProc("il2cpp_type_get_object"));
    g_api.arrayLength = reinterpret_cast<ArrayLengthFn>(GetProc("il2cpp_array_length"));
    g_api.stringChars = reinterpret_cast<StringCharsFn>(GetProc("il2cpp_string_chars"));
    g_api.stringLength = reinterpret_cast<StringLengthFn>(GetProc("il2cpp_string_length"));
    g_api.stringNewWrapper =
        reinterpret_cast<StringNewWrapperFn>(GetProc("il2cpp_string_new_wrapper"));
    g_api.ok = g_api.runtimeInvoke && g_api.classFromName && g_api.imageFromAssembly &&
               g_api.domainGetAssemblies && g_api.classGetMethod && g_api.classGetType &&
               g_api.typeGetObject &&
               g_api.arrayLength && g_api.stringChars && g_api.stringLength &&
               g_api.stringNewWrapper;
    if (!g_api.ok) {
        Log(LogLevel::Warn, "unity scanner: il2cpp API 解析不完整");
    }
    return g_api.ok;
}

std::vector<void*> EnumerateImages() {
    std::vector<void*> images;
    void* domain = nullptr;
    if (void* fn = GetProc("il2cpp_domain_get")) {
        domain = reinterpret_cast<void* (*)()>(fn)();
    }
    size_t count = 0;
    void** assemblies = static_cast<void**>(g_api.domainGetAssemblies(domain, &count));
    for (size_t i = 0; i < count; i++) {
        if (void* image = g_api.imageFromAssembly(assemblies[i])) {
            images.push_back(image);
        }
    }
    return images;
}

bool ResolveTargets() {
    auto images = EnumerateImages();
    Log(LogLevel::Info, "unity scanner: %zu assemblies", images.size());
    auto tryAdd = [&](void* klass, const char* label) {
        if (!klass) {
            return;
        }
        void* getText = g_api.classGetMethod(klass, "get_text", 0);
        void* setText = g_api.classGetMethod(klass, "set_text", 1);
        if (getText && setText) {
            g_targets.push_back({klass, getText, setText, label});
            Log(LogLevel::Info, "unity scanner target ready: %s", label);
        }
    };
    for (void* image : images) {
        tryAdd(g_api.classFromName(image, "UnityEngine.UI", "Text"), "UnityEngine.UI.Text");
        tryAdd(g_api.classFromName(image, "TMPro", "TMP_Text"), "TMPro.TMP_Text");
    }
    for (void* image : images) {
        g_objectClass = g_api.classFromName(image, "UnityEngine", "Object");
        if (g_objectClass) {
            g_findMethod = g_api.classGetMethod(g_objectClass, "FindObjectsOfType", 1);
            break;
        }
    }
    if (g_findMethod) {
        Log(LogLevel::Info, "unity scanner FindObjectsOfType ready");
    } else {
        Log(LogLevel::Warn, "unity scanner: FindObjectsOfType 未找到");
    }
    return !g_targets.empty();
}

std::string Il2CppStringToUtf8(void* str) {
    if (!str) {
        return {};
    }
    int len = g_api.stringLength(str);
    if (len <= 0) {
        return {};
    }
    return WidenToUtf8(static_cast<const wchar_t*>(g_api.stringChars(str)), len);
}

void* ElementAt(void* array, size_t index) {
    if (!array) {
        return nullptr;
    }
    auto readable = [](void* p) {
        MEMORY_BASIC_INFORMATION mbi{};
        return p && VirtualQuery(p, &mbi, sizeof(mbi)) != 0 &&
               mbi.State == MEM_COMMIT &&
               (mbi.Protect & (PAGE_READWRITE | PAGE_READONLY | PAGE_EXECUTE_READWRITE |
                               PAGE_EXECUTE_READ)) != 0;
    };
    HMODULE ga = GetModuleHandleW(L"GameAssembly.dll");
    uintptr_t base = reinterpret_cast<uintptr_t>(ga);
    auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(ga);
    auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(
        reinterpret_cast<const uint8_t*>(ga) + dos->e_lfanew);
    uintptr_t end = base + nt->OptionalHeader.SizeOfImage;
    void** items = nullptr;
    for (size_t off = 0; off <= 64; off += sizeof(void*)) {
        void* candidate = *reinterpret_cast<void**>(static_cast<uint8_t*>(array) + off);
        if (!readable(candidate)) {
            continue;
        }
        uintptr_t candAddr = reinterpret_cast<uintptr_t>(candidate);
        if (candAddr >= base && candAddr < end) {
            continue;  // 模块内指针（如 klass），不是元素数组
        }
        uintptr_t firstKlass = *reinterpret_cast<uintptr_t*>(candidate);
        if (firstKlass < base || firstKlass >= end) {
            continue;  // 元素 0 不是合法对象（klass 不在模块内）
        }
        items = static_cast<void**>(candidate);
        break;
    }
    if (!items) {
        return nullptr;
    }
    return items[index];
}

size_t SafeArrayLength(void* array) {
    size_t len = 0;
    __try {
        len = g_api.arrayLength(array);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        len = 0;
    }
    return len;
}

void RunScanIfRequested() {
    if (g_scanRequested.exchange(false) && !g_scanInProgress.exchange(true)) {
        __try {
            ScanOnePass();
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            Log(LogLevel::Warn, "unity scanner caught exception code=0x%08X", GetExceptionCode());
        }
        g_scanInProgress.store(false);
    }
}

void ScanOnePass() {
    if (!g_findMethod) {
        return;
    }
    for (const auto& target : g_targets) {
        Log(LogLevel::Debug, "unity scanner: scan begin %s", target.label.c_str());
        void* typeObj = g_api.typeGetObject(g_api.classGetType(target.klass));
        void* params[1] = {typeObj};
        void* exception = nullptr;
        void* array = g_api.runtimeInvoke(g_findMethod, nullptr, params, &exception);
        Log(LogLevel::Debug, "unity scanner: FindObjectsOfType done %s array=%p exc=%d",
            target.label.c_str(), array, exception ? 1 : 0);
        if (!array || exception) {
            Log(LogLevel::Warn, "unity scanner: FindObjectsOfType failed for %s",
                target.label.c_str());
            continue;
        }
        size_t len = SafeArrayLength(array);
        if (len == 0 || len > 100000) {
            // 兜底：手动读取 max_length（obj 2*ptr + bounds ptr 之后）
            size_t maxLenOffset = 3 * sizeof(void*);
            len = *reinterpret_cast<size_t*>(static_cast<uint8_t*>(array) + maxLenOffset);
        }
        Log(LogLevel::Debug, "unity scanner: array len=%zu", len);
        if (len == 0) {
            continue;
        }
        static std::map<std::string, bool> logged;
        if (!logged[target.label]) {
            logged[target.label] = true;
            Log(LogLevel::Info, "unity scanner: %s objects=%zu", target.label.c_str(), len);
        }
        const size_t maxPerTick = 300;
        size_t processed = 0;
        size_t translated = 0;
        size_t diagLogged = 0;
        for (size_t i = 0; i < len && processed < maxPerTick; i++) {
            void* obj = ElementAt(array, i);
            if (!obj) {
                continue;
            }
            processed++;
            void* textObj = g_api.runtimeInvoke(target.getText, obj, nullptr, &exception);
            if (!textObj || exception) {
                exception = nullptr;
                continue;
            }
            std::string source = Il2CppStringToUtf8(textObj);
            std::string reason;
            if (source.empty() || !gTextFilter.ShouldTranslate(source, &reason)) {
                if (diagLogged < 5) {
                    diagLogged++;
                    Log(LogLevel::Info, "unity scanner: %s 跳过 obj=%p text='%s' reason=%s",
                        target.label.c_str(), obj,
                        source.empty() ? "(空)" : source.c_str(),
                        source.empty() ? "empty" : reason.c_str());
                }
                continue;
            }
            if (diagLogged < 5) {
                diagLogged++;
                Log(LogLevel::Info, "unity scanner: %s 命中 obj=%p text='%s'", target.label.c_str(),
                    obj, source.c_str());
            }
            uint64_t hash = Fnv1a64(source);
            uint64_t nowMs = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch())
                    .count());
            {
                std::lock_guard<std::mutex> lock(g_seenMutex);
                auto it = g_seen.find(obj);
                if (it != g_seen.end() && it->second.first == hash &&
                    nowMs < it->second.second) {
                    continue;
                }
            }
            std::string targetText;
            std::string origin;
            if (IpcClient::Instance().RequestTranslation(
                    source, EngineId(GtiEngine::UnityIL2CPP), &targetText, &origin,
                    IpcClient::Instance().blockTimeoutMs()) &&
                !targetText.empty()) {
                bool applied = false;
                if (!std::getenv("GTI_SCANNER_READONLY")) {
                    if (void* newString = g_api.stringNewWrapper(targetText.c_str())) {
                        void* setParams[1] = {newString};
                        exception = nullptr;
                        g_api.runtimeInvoke(target.setText, obj, setParams, &exception);
                        applied = exception == nullptr;
                    }
                } else {
                    applied = true;
                }
                if (applied) {
                    translated++;
                    std::lock_guard<std::mutex> lock(g_seenMutex);
                    g_seen[obj] = {hash, nowMs + 3600000};
                    continue;
                }
            }
            {
                std::lock_guard<std::mutex> lock(g_seenMutex);
                if (g_seen.size() >= kSeenCapacity) {
                    g_seen.clear();
                }
                g_seen[obj] = {hash, nowMs + 60000};
            }
        }
        if (translated > 0) {
            Log(LogLevel::Info, "unity scanner: %s scanned=%zu translated=%zu",
                target.label.c_str(), processed, translated);
        }
    }
}

// ---- 主线程触发器：hook Time.get_time / get_deltaTime / get_realtimeSinceStartup ----

using TimeGetFn = float(__cdecl*)();
TimeGetFn g_origDeltaTime = nullptr;
TimeGetFn g_origRealtime = nullptr;
TimeGetFn g_origTime = nullptr;

float TimeDetourImpl(TimeGetFn orig) {
    static std::atomic<bool> g_firedLogged{false};
    if (!g_firedLogged.exchange(true)) {
        Log(LogLevel::Info, "unity scanner: Time detour fired");
    }
    RunScanIfRequested();
    return orig();
}

float __cdecl DetourDeltaTime() {
    return TimeDetourImpl(g_origDeltaTime);
}

float __cdecl DetourRealtime() {
    return TimeDetourImpl(g_origRealtime);
}

float __cdecl DetourTime() {
    return TimeDetourImpl(g_origTime);
}

// ---- Windows 定时器主线程触发器（最可靠，不依赖游戏脚本调用） ----

HWND g_mainWindow = nullptr;
UINT_PTR g_timerId = 0;

BOOL CALLBACK FindMainWindowProc(HWND hwnd, LPARAM lParam) {
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == static_cast<DWORD>(lParam) && IsWindowVisible(hwnd)) {
        g_mainWindow = hwnd;
        return FALSE;
    }
    return TRUE;
}

void CALLBACK ScanTimerProc(HWND, UINT, UINT_PTR, DWORD) {
    RunScanIfRequested();
}

bool InstallWindowTimer() {
    DWORD pid = GetCurrentProcessId();
    EnumWindows(FindMainWindowProc, static_cast<LPARAM>(pid));
    if (!g_mainWindow) {
        Log(LogLevel::Warn, "unity scanner: 未找到游戏主窗口，稍后重试");
        return false;
    }
    g_timerId = SetTimer(g_mainWindow, 0x4754, 100, ScanTimerProc);
    Log(LogLevel::Info, "unity scanner: 主窗口定时器已挂 hwnd=%p timer=%p", g_mainWindow,
        reinterpret_cast<void*>(g_timerId));
    return g_timerId != 0;
}

void* FindNativePointer(void* methodInfo) {
    if (!methodInfo) {
        return nullptr;
    }
    HMODULE ga = GetModuleHandleW(L"GameAssembly.dll");
    if (!ga) {
        return nullptr;
    }
    auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(ga);
    auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(
        reinterpret_cast<const uint8_t*>(ga) + dos->e_lfanew);
    uintptr_t base = reinterpret_cast<uintptr_t>(ga);
    uintptr_t end = base + nt->OptionalHeader.SizeOfImage;
    for (int offset = 0; offset <= 128; offset += static_cast<int>(sizeof(void*))) {
        uintptr_t candidate = *reinterpret_cast<uintptr_t*>(
            reinterpret_cast<uint8_t*>(methodInfo) + offset);
        if (candidate >= base && candidate < end) {
            return reinterpret_cast<void*>(candidate);
        }
    }
    return nullptr;
}

bool InstallMainThreadTrigger() {
    auto images = EnumerateImages();
    void* timeClass = nullptr;
    for (void* image : images) {
        timeClass = g_api.classFromName(image, "UnityEngine", "Time");
        if (timeClass) {
            break;
        }
    }
    if (!timeClass) {
        Log(LogLevel::Warn, "unity scanner: UnityEngine.Time 未找到");
        return false;
    }
    // deltaTime 是 Unity 每帧最常被调用的取值器，优先挂它。
    struct Candidate {
        const char* name;
        void* detour;
        TimeGetFn* orig;
    };
    Candidate candidates[] = {
        {"get_deltaTime", reinterpret_cast<void*>(&DetourDeltaTime), &g_origDeltaTime},
        {"get_realtimeSinceStartup", reinterpret_cast<void*>(&DetourRealtime), &g_origRealtime},
        {"get_time", reinterpret_cast<void*>(&DetourTime), &g_origTime},
    };
    int hooked = 0;
    for (auto& candidate : candidates) {
        const char* name = candidate.name;
        void* methodInfo = g_api.classGetMethod(timeClass, name, 0);
        void* native = methodInfo ? FindNativePointer(methodInfo) : nullptr;
        if (!native) {
            continue;
        }
        auto* bytes = static_cast<const uint8_t*>(native);
        Log(LogLevel::Info, "unity scanner candidate %s native=%p bytes=%02X %02X %02X %02X %02X",
            name, native, bytes[0], bytes[1], bytes[2], bytes[3], bytes[4]);
        if (HookManager::Install(
                (std::string("unity_scanner.") + name).c_str(), native, candidate.detour,
                reinterpret_cast<void**>(candidate.orig))) {
            hooked++;
        }
    }
    Log(LogLevel::Info, "unity scanner main-thread triggers hooked: %d/3", hooked);
    return hooked > 0;
}

DWORD WINAPI ScannerThreadProc(LPVOID) {
    Log(LogLevel::Info, "unity scanner requester started");
    while (g_scannerRunning.load() && HooksActive()) {
        if (!g_mainWindow || !g_timerId) {
            InstallWindowTimer();
        }
        g_scanRequested.store(true);
        for (int i = 0; i < 30 && g_scannerRunning.load(); i++) {
            Sleep(100);  // 最多等 3 秒让主线程执行
        }
        for (int i = 0; i < 20 && g_scannerRunning.load(); i++) {
            Sleep(100);  // 间隔约 5 秒一轮
        }
    }
    Log(LogLevel::Info, "unity scanner requester stopped");
    return 0;
}

}  // namespace

void StartUnityScanner() {
    if (g_scannerRunning.load()) {
        return;
    }
    if (!ResolveApi()) {
        return;
    }
    g_targets.clear();
    if (!ResolveTargets()) {
        Log(LogLevel::Warn, "unity scanner disabled: 未找到 Text/TMP_Text 目标");
        return;
    }
    if (!InstallMainThreadTrigger()) {
        Log(LogLevel::Info, "unity scanner: Time 触发器不可用，继续尝试窗口定时器");
    }
    if (!InstallWindowTimer()) {
        Log(LogLevel::Warn, "unity scanner: 主窗口定时器暂未挂上，requester 线程将重试");
    }
    g_scannerRunning.store(true);
    g_scannerThread = CreateThread(nullptr, 0, ScannerThreadProc, nullptr, 0, nullptr);
    Log(LogLevel::Info, "unity scanner started");
}

void StopUnityScanner() {
    g_scannerRunning.store(false);
    g_scanRequested.store(false);
    if (g_timerId && g_mainWindow) {
        KillTimer(g_mainWindow, g_timerId);
    }
    g_timerId = 0;
    g_mainWindow = nullptr;
    if (g_scannerThread) {
        WaitForSingleObject(g_scannerThread, 2000);
        CloseHandle(g_scannerThread);
        g_scannerThread = nullptr;
    }
}

}  // namespace gti
