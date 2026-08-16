#include "gti/ipc_client.h"

#include <algorithm>
#include <chrono>
#include <thread>

#include <windows.h>

#include "gti/engine.h"
#include "gti/gti_version.h"
#include "gti/log.h"
#include "gti/string_util.h"

namespace gti {
namespace {

constexpr uint32_t kHeartbeatMs = 2000;
constexpr uint32_t kReconnectBaseMs = 1000;
constexpr uint32_t kReconnectMaxMs = 8000;

std::wstring PipeNameFor(const std::wstring& pipeName) {
    if (pipeName.rfind(L"\\\\.\\pipe\\", 0) == 0) {
        return pipeName;
    }
    return L"\\\\.\\pipe\\" + pipeName;
}

}  // namespace

DWORD WINAPI IpcClient::ControlThreadProc(LPVOID param) {
    static_cast<IpcClient*>(param)->ControlLoop();
    return 0;
}

IpcClient& IpcClient::Instance() {
    static IpcClient instance;
    return instance;
}

bool IpcClient::ConnectOnce(const std::wstring& pipeName) {
    ClosePipe();
    HANDLE pipe = CreateFileW(
        PipeNameFor(pipeName).c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (pipe == INVALID_HANDLE_VALUE) {
        Log(LogLevel::Debug, "IPC connect failed: %ls err=%lu", PipeNameFor(pipeName).c_str(),
            GetLastError());
        return false;
    }
    Log(LogLevel::Debug, "IPC connect ok: %ls", PipeNameFor(pipeName).c_str());
    pipe_ = pipe;
    connected_.store(true);

    std::vector<uint8_t> payload;
    wire::PutU32(payload, 0);
    wire::PutU32(payload, GetCurrentProcessId());
    wire::PutU32(payload, sizeof(void*) == 8 ? 2u : 1u);
    EngineInfo info = DetectEngine();
    wire::PutU32(payload, EngineId(info.engine));
    wire::PutU32(payload, GTI_PROTOCOL_VERSION);
    std::string name = std::string(info.name.empty() ? EngineName(info.engine) : info.name) +
                       " v" GTI_VERSION_STRING;
    wire::PutString(payload, name);
    SendFrame(MsgType::Hello, 0, payload.data(), static_cast<uint32_t>(payload.size()));
    Log(LogLevel::Info, "IPC connected: %ls (engine=%s)", PipeNameFor(pipeName).c_str(),
        name.c_str());
    return true;
}

void IpcClient::ClosePipe() {
    connected_.store(false);
    if (pipe_ != INVALID_HANDLE_VALUE) {
        CloseHandle(pipe_);
        pipe_ = INVALID_HANDLE_VALUE;
    }
}

bool IpcClient::SendFrame(MsgType type, uint16_t flags, const void* payload, uint32_t len) {
    std::lock_guard<std::mutex> lock(writeMutex_);
    return SendFrameLocked(type, flags, payload, len);
}

bool IpcClient::SendFrameLocked(MsgType type, uint16_t flags, const void* payload,
                                uint32_t len) {
    if (!connected_.load() || pipe_ == INVALID_HANDLE_VALUE) {
        return false;
    }
    std::vector<uint8_t> frame = BuildFrame(type, flags, payload, len);
    DWORD written = 0;
    if (!WriteFile(pipe_, frame.data(), static_cast<DWORD>(frame.size()), &written,
                   nullptr)) {
        connected_.store(false);
        return false;
    }
    return written == frame.size();
}

bool IpcClient::ReadFrame(GtiFrame* frame) {
    uint8_t headerBytes[sizeof(GtiHeader)];
    DWORD read = 0;
    if (!ReadFile(pipe_, headerBytes, sizeof(headerBytes), &read, nullptr) ||
        read != sizeof(headerBytes)) {
        return false;
    }
    GtiHeader header{};
    std::memcpy(&header, headerBytes, sizeof(header));
    if (!IsValidMagic(header) || header.payloadLen > kMaxPayload) {
        return false;
    }
    std::vector<uint8_t> full(sizeof(GtiHeader) + header.payloadLen);
    std::memcpy(full.data(), headerBytes, sizeof(headerBytes));
    if (header.payloadLen > 0) {
        read = 0;
        if (!ReadFile(pipe_, full.data() + sizeof(headerBytes), header.payloadLen, &read,
                      nullptr) ||
            read != header.payloadLen) {
            return false;
        }
    }
    uint32_t error = 0;
    return FrameFromBytes(full.data(), full.size(), frame, &error);
}

bool IpcClient::ReadFrameWithTimeout(GtiFrame* frame, uint32_t timeoutMs) {
    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        DWORD available = 0;
        if (!PeekNamedPipe(pipe_, nullptr, 0, nullptr, &available, nullptr)) {
            connected_.store(false);
            return false;
        }
        if (available >= sizeof(GtiHeader)) {
            return ReadFrame(frame);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

void IpcClient::HandleFrame(const GtiFrame& frame) {
    const uint8_t* p = frame.payload.data();
    const size_t n = frame.payload.size();
    switch (static_cast<MsgType>(frame.header.type)) {
        case MsgType::HelloAck: {
            if (n < 16) {
                return;
            }
            uint32_t timeout = wire::GetU32(p + 8);
            uint32_t filters = wire::GetU32(p + 12);
            if (timeout >= 100 && timeout <= 60000) {
                blockTimeoutMs_.store(timeout);
            }
            filtersEnabled_.store(filters != 0);
            Log(LogLevel::Info, "IPC handshake ok timeout=%u filters=%u", timeout, filters);
            break;
        }
        case MsgType::Unload: {
            Log(LogLevel::Info, "unload requested by UI");
            std::vector<uint8_t> payload;
            wire::PutU32(payload, n >= 4 ? wire::GetU32(p) : 0);
            wire::PutU32(payload, 1);
            SendFrameLocked(MsgType::UnloadAck, kFlagResponse, payload.data(),
                            static_cast<uint32_t>(payload.size()));
            std::function<void()> handler;
            {
                std::lock_guard<std::mutex> lock(handlerMutex_);
                handler = unloadHandler_;
            }
            if (handler) {
                handler();
            }
            break;
        }
        case MsgType::TextResult:
        case MsgType::Pong:
        case MsgType::Stats:
        case MsgType::Log:
        default:
            break;
    }
}

void IpcClient::ControlLoop() {
    uint32_t backoff = kReconnectBaseMs;
    auto lastStats = std::chrono::steady_clock::now();
    while (running_.load()) {
        if (!connected_.load()) {
            if (ConnectOnce(pipeName_)) {
                backoff = kReconnectBaseMs;
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(backoff));
                backoff = std::min(backoff * 2, kReconnectMaxMs);
            }
            continue;
        }

        // Drain UI->DLL control frames (UNLOAD/PONG) between requests. The peek
        // and the read happen under the SAME lock so a request thread can never
        // consume a frame between our peek and read; without that, the read
        // would block forever holding the mutex and freeze every game thread.
        std::lock_guard<std::mutex> lock(writeMutex_);
        DWORD available = 0;
        if (PeekNamedPipe(pipe_, nullptr, 0, nullptr, &available, nullptr) &&
            available >= sizeof(GtiHeader)) {
            GtiFrame frame;
            if (ReadFrame(&frame)) {
                HandleFrame(frame);
            }
        }
        auto now = std::chrono::steady_clock::now();
        if (now - lastStats >= std::chrono::seconds(30)) {
            lastStats = now;
            Log(LogLevel::Info, "IPC stats: captured=%llu translated=%llu skipped=%llu",
                captured.load(), translated.load(), skipped.load());
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

bool IpcClient::Start(const std::wstring& pipeName) {
    Stop();
    pipeName_ = pipeName;
    running_.store(true);
    hControl_ = CreateThread(nullptr, 0, ControlThreadProc, this, 0, nullptr);
    return hControl_ != nullptr;
}

void IpcClient::Stop() {
    if (!running_.exchange(false)) {
        return;
    }
    ClosePipe();
    if (hControl_) {
        WaitForSingleObject(hControl_, 2000);
        CloseHandle(hControl_);
        hControl_ = nullptr;
    }
}

bool IpcClient::WaitForIdle(uint32_t timeoutMs) {
    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        if (writeMutex_.try_lock()) {
            writeMutex_.unlock();
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return false;
}

bool IpcClient::RequestTranslation(const std::string& source, uint32_t engine,
                                   std::string* outTarget, std::string* outOrigin,
                                   uint32_t timeoutMs) {
    // Serialized full exchange: writes and reads never overlap other threads.
    std::lock_guard<std::mutex> lock(writeMutex_);
    if (!connected_.load() || pipe_ == INVALID_HANDLE_VALUE) {
        return false;
    }

    // Rolling stall budget: when the UI/translator is slow, cap how long the
    // game threads may block each second so the game stays responsive.
    constexpr int32_t kBudgetMs = 250;
    static std::atomic<int64_t> gWindowStartTick{0};
    static std::atomic<int32_t> gWindowBlockedMs{0};
    int64_t nowTick = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::steady_clock::now().time_since_epoch())
                          .count();
    int64_t windowStart = gWindowStartTick.load();
    if (nowTick - windowStart >= 1000) {
        gWindowStartTick.store(nowTick);
        gWindowBlockedMs.store(0);
    }
    int32_t used = gWindowBlockedMs.load();
    if (used >= kBudgetMs) {
        return false;
    }
    int32_t allowed = std::max(1, kBudgetMs - used);
    uint32_t effectiveTimeout = std::min(timeoutMs, static_cast<uint32_t>(allowed));
    auto stallStart = std::chrono::steady_clock::now();

    uint32_t corr = nextCorr_.fetch_add(1);
    std::vector<uint8_t> payload;
    wire::PutU32(payload, corr);
    wire::PutU64(payload, Fnv1a64(source));
    wire::PutU32(payload, engine);
    wire::PutString(payload, source);
    if (!SendFrameLocked(MsgType::TextRequest, 0, payload.data(),
                         static_cast<uint32_t>(payload.size()))) {
        Log(LogLevel::Warn, "IPC request send failed corr=%u", corr);
        return false;
    }
    Log(LogLevel::Debug, "IPC request sent corr=%u engine=%u len=%zu", corr, engine,
        source.size());

    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(effectiveTimeout);
    while (std::chrono::steady_clock::now() < deadline) {
        uint32_t remaining = static_cast<uint32_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now())
                .count());
        GtiFrame response;
        if (!ReadFrameWithTimeout(&response, std::max(remaining, 1u))) {
            connected_.store(false);
            Log(LogLevel::Warn, "IPC request read failed/timeout corr=%u", corr);
            int64_t elapsed =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - stallStart)
                    .count();
            gWindowBlockedMs.fetch_add(static_cast<int32_t>(std::min<int64_t>(elapsed, 1000)));
            return false;
        }
        if (static_cast<MsgType>(response.header.type) == MsgType::TextResult) {
            const uint8_t* rp = response.payload.data();
            size_t rn = response.payload.size();
            if (rn < 20) {
                int64_t elapsed =
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - stallStart)
                        .count();
                gWindowBlockedMs.fetch_add(
                    static_cast<int32_t>(std::min<int64_t>(elapsed, 1000)));
                return false;
            }
            uint32_t ok = wire::GetU32(rp + 12);
            Log(LogLevel::Debug, "IPC result corr=%u ok=%u len=%zu", corr, ok, rn);
            if (ok && outTarget) {
                *outTarget = std::string(reinterpret_cast<const char*>(rp + 20), rn - 20);
            }
            if (outOrigin) {
                uint8_t origin = rp[16];
                *outOrigin = origin == 1 ? "cache" : origin == 2 ? "dict"
                                                                 : origin == 3 ? "ai" : "manual";
            }
            int64_t elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                                  std::chrono::steady_clock::now() - stallStart)
                                  .count();
            gWindowBlockedMs.fetch_add(static_cast<int32_t>(std::min<int64_t>(elapsed, 1000)));
            return ok != 0;
        }
        HandleFrame(response);  // e.g. UNLOAD delivered while a request is in flight
    }
    int64_t elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::steady_clock::now() - stallStart)
                          .count();
    gWindowBlockedMs.fetch_add(static_cast<int32_t>(std::min<int64_t>(elapsed, 1000)));
    Log(LogLevel::Warn, "IPC request timed out corr=%u timeout=%ums", corr, effectiveTimeout);
    return false;
}

// Log frames over the shared pipe are intentionally disabled: diagnostic traffic
// can fill the pipe buffer and deadlock the request path. Native logs go to the
// optional GTI_LOG_FILE and OutputDebugString only.
void IpcClient::SendLogMessage(LogLevel, const char*) {}

void IpcClient::SetUnloadHandler(std::function<void()> handler) {
    std::lock_guard<std::mutex> lock(handlerMutex_);
    unloadHandler_ = std::move(handler);
}

}  // namespace gti
