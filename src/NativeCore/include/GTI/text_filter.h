#pragma once

#include <chrono>
#include <cstddef>
#include <mutex>
#include <string>
#include <unordered_map>

namespace gti {

struct TextFilterConfig {
    size_t minLen = 1;
    size_t maxLen = 1024;
    bool skipCjk = true;
    bool skipPaths = true;
    size_t rateLimitPerSec = 200;
    size_t dedupeCapacity = 8192;
};

class TextFilter {
public:
    void SetConfig(const TextFilterConfig& cfg);
    const TextFilterConfig& config() const { return cfg_; }
    bool ShouldTranslate(const std::string& text, std::string* reason) const;

private:
    bool RateLimitedLocked() const;

    TextFilterConfig cfg_;
    mutable std::mutex mutex_;
    mutable std::chrono::steady_clock::time_point windowStart_{};
    mutable size_t windowCount_ = 0;
    mutable std::unordered_map<std::string, std::chrono::steady_clock::time_point> recent_;
};

extern TextFilter gTextFilter;

}  // namespace gti
