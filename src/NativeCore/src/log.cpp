#include "gti/log.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <vector>

#include <windows.h>

namespace gti {
namespace {

std::mutex gLogMutex;
LogSink gLogSink = nullptr;
std::vector<std::string> gRing;
constexpr size_t kRingCapacity = 256;

void AppendToRing(const char* message) {
    gRing.push_back(message);
    if (gRing.size() > kRingCapacity) {
        gRing.erase(gRing.begin(), gRing.begin() + static_cast<long>(gRing.size() - kRingCapacity));
    }
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

    // Optional file log for diagnostics (opt-in via GTI_LOG_FILE).
    if (const char* logPath = std::getenv("GTI_LOG_FILE"); logPath && logPath[0]) {
        std::lock_guard<std::mutex> lock(gLogMutex);
        std::ofstream file(logPath, std::ios::app);
        if (file) {
            file << line << '\n';
        }
    }

    LogSink sink = nullptr;
    {
        std::lock_guard<std::mutex> lock(gLogMutex);
        AppendToRing(line.c_str());
        sink = gLogSink;
    }
    if (sink) {
        sink(level, line.c_str());
    }
}

}  // namespace gti
