#include "gti/text_filter.h"

#include "gti/string_util.h"

namespace gti {

TextFilter gTextFilter;

void TextFilter::SetConfig(const TextFilterConfig& cfg) {
    std::lock_guard<std::mutex> lock(mutex_);
    cfg_ = cfg;
    windowStart_ = {};
    windowCount_ = 0;
    recent_.clear();
}

bool TextFilter::RateLimitedLocked() const {
    auto now = std::chrono::steady_clock::now();
    if (windowStart_.time_since_epoch().count() == 0) {
        windowStart_ = now;
        windowCount_ = 0;
    }
    if (now - windowStart_ >= std::chrono::seconds(1)) {
        windowStart_ = now;
        windowCount_ = 0;
    }
    if (cfg_.rateLimitPerSec > 0 && windowCount_ >= cfg_.rateLimitPerSec) {
        return true;
    }
    ++windowCount_;
    return false;
}

bool TextFilter::ShouldTranslate(const std::string& text, std::string* reason) const {
    if (text.empty()) {
        if (reason) *reason = "empty";
        return false;
    }
    if (text.size() < cfg_.minLen || text.size() > cfg_.maxLen) {
        if (reason) *reason = "length";
        return false;
    }
    if (cfg_.skipCjk && HasCjkUtf8(text)) {
        if (reason) *reason = "already-cjk";
        return false;
    }
    if (!HasAsciiLetter(text)) {
        if (reason) *reason = "no-letters";
        return false;
    }
    if (cfg_.skipPaths && LooksLikePathOrUrl(text)) {
        if (reason) *reason = "path-or-url";
        return false;
    }
    if (IsMostlyAsciiControl(text)) {
        if (reason) *reason = "control";
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto now = std::chrono::steady_clock::now();
    auto it = recent_.find(text);
    if (it != recent_.end()) {
        it->second = now;
        if (reason) *reason = "recent-dedupe";
        return false;
    }
    if (recent_.size() >= cfg_.dedupeCapacity) {
        recent_.clear();
    }
    recent_.emplace(text, now);
    if (RateLimitedLocked()) {
        if (reason) *reason = "rate-limit";
        return false;
    }
    return true;
}

}  // namespace gti
