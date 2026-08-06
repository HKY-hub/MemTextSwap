#pragma once

#include <cstdint>
#include <string>

namespace gti {

enum class LogLevel : uint8_t {
    Debug = 1,
    Info = 2,
    Warn = 3,
    Error = 4,
};

using LogSink = void (*)(LogLevel, const char*);

void Log(LogLevel level, const char* fmt, ...);
void SetLogSink(LogSink sink);
std::string LogLevelName(LogLevel level);

}  // namespace gti
