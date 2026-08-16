#include "gti/log.h"

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

#include <windows.h>

namespace gti {
namespace {

std::mutex gLogMutex;
LogSink gLogSink = nullptr;
std::vector<std::string> gRing;
constexpr size_t kRingCapacity = 256;
std::string gLogFilePath;
std::atomic<bool> gLogFileReady{false};

void AppendToRing(const char* message) {
    gRing.push_back(message);
    if (gRing.size() > kRingCapacity) {
        gRing.erase(gRing.begin(), gRing.begin() + static_cast<long>(gRing.size() - kRingCapacity));
    }
}

// 默认日志路径：%APPDATA%\MemTextSwap\logs\native.log（可用 GTI_LOG_FILE 覆盖）。
// 只保留最新日志：进程内首次写日志时清空旧文件。
void InitLogFile() {
    if (gLogFileReady.load()) {
        return;
    }
    gLogFileReady.store(true);
    std::string path;
    if (const char* overridePath = std::getenv("GTI_LOG_FILE"); overridePath && overridePath[0]) {
        path = overridePath;
    } else {
        char appData[MAX_PATH] = {};
        if (GetEnvironmentVariableA("APPDATA", appData, MAX_PATH) > 0) {
            std::string base = appData;
            base += "\\MemTextSwap";
            CreateDirectoryA(base.c_str(), nullptr);
            base += "\\logs";
            CreateDirectoryA(base.c_str(), nullptr);
            path = base + "\\native.log";
        } else {
            path = "native.log";
        }
    }
    gLogFilePath = path;
    std::ofstream file(path, std::ios::trunc);
}

}  // namespace

void SetLogSink(LogSink sink) {
    std::lock_guard<std::mutex> lock(gLogMutex);
    gLogSink = sink;
}

std::string LogLevelName(LogLevel level) {
    switch (level) {
        case LogLevel::Debug:
            return "DEBUG";
        case LogLevel::Info:
            return "INFO";
        case LogLevel::Warn:
            return "WARN";
        case LogLevel::Error:
            return "ERROR";
    }
    return "UNKNOWN";
}

void Log(LogLevel level, const char* fmt, ...) {
    char buffer[2048];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    std::string line = "[" + LogLevelName(level) + "] " + buffer;
    OutputDebugStringA(line.c_str());

    {
        std::lock_guard<std::mutex> lock(gLogMutex);
        InitLogFile();
        if (!gLogFilePath.empty()) {
            SYSTEMTIME st{};
            GetLocalTime(&st);
            char prefix[160] = {};
            std::snprintf(prefix, sizeof(prefix),
                          "[%04u-%02u-%02u %02u:%02u:%02u.%03u] [T%05lu] ",
                          st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
                          st.wMilliseconds, GetCurrentThreadId());
            std::ofstream file(gLogFilePath, std::ios::app);
            if (file) {
                file << prefix << line << '\n';
            }
        }
        AppendToRing(line.c_str());
        LogSink sink = gLogSink;
        if (sink) {
            sink(level, line.c_str());
        }
    }
}

}  // namespace gti
