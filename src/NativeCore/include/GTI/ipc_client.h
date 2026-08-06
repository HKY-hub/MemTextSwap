#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>

#include <windows.h>

#include "gti/ipc_protocol.h"

namespace gti {

enum class LogLevel : uint8_t;

class IpcClient {
public:
    static IpcClient& Instance();

    bool Start(const std::wstring& pipeName);
    void Stop();
    bool WaitForIdle(uint32_t timeoutMs);
    bool IsConnected() const { return connected_.load(); }

    // Blocking translation request (up to timeoutMs). Returns false if offline/timeout.
    bool RequestTranslation(const std::string& source, uint32_t engine,
                            std::string* outTarget, std::string* outOrigin,
                            uint32_t timeoutMs);

    void SendLogMessage(LogLevel level, const char* message);
    void SetUnloadHandler(std::function<void()> handler);

    uint32_t blockTimeoutMs() const { return blockTimeoutMs_.load(); }
    bool filtersEnabled() const { return filtersEnabled_.load(); }

    std::atomic<uint64_t> captured{0};
    std::atomic<uint64_t> translated{0};
    std::atomic<uint64_t> skipped{0};

private:
    IpcClient() = default;
    ~IpcClient() = default;
    IpcClient(const IpcClient&) = delete;
    IpcClient& operator=(const IpcClient&) = delete;

    bool ConnectOnce(const std::wstring& pipeName);
    void ClosePipe();
    bool SendFrame(MsgType type, uint16_t flags, const void* payload, uint32_t len);
    bool SendFrameLocked(MsgType type, uint16_t flags, const void* payload, uint32_t len);
    bool ReadFrame(GtiFrame* frame);
    bool ReadFrameWithTimeout(GtiFrame* frame, uint32_t timeoutMs);
    void HandleFrame(const GtiFrame& frame);
    void ControlLoop();

    static DWORD WINAPI ControlThreadProc(LPVOID param);

    HANDLE pipe_ = INVALID_HANDLE_VALUE;
    std::wstring pipeName_;
    std::atomic<bool> connected_{false};
    std::atomic<bool> running_{false};
    std::atomic<uint32_t> blockTimeoutMs_{3000};
    std::atomic<bool> filtersEnabled_{true};
    std::atomic<uint32_t> nextCorr_{1};
    HANDLE hControl_ = nullptr;

    mutable std::mutex writeMutex_;
    std::mutex handlerMutex_;
    std::function<void()> unloadHandler_;
};

}  // namespace gti
