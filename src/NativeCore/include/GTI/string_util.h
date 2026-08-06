#pragma once

#include <cstdint>
#include <string>

namespace gti {

std::string WidenToUtf8(const wchar_t* text, int len = -1);
std::wstring Utf8ToWiden(const char* text, int len = -1);

// FNV-1a 64-bit, stable across C++ and C#.
inline uint64_t Fnv1a64(const std::string& s) {
    uint64_t hash = 14695981039346656037ULL;
    for (unsigned char c : s) {
        hash ^= c;
        hash *= 1099511628211ULL;
    }
    return hash;
}

// Decode one UTF-8 code point; returns cp and advances pos.
bool DecodeUtf8(const std::string& s, size_t* pos, uint32_t* cp);
bool HasCjkUtf8(const std::string& s);
bool HasAsciiLetter(const std::string& s);
bool LooksLikePathOrUrl(const std::string& s);
bool IsMostlyAsciiControl(const std::string& s);

}  // namespace gti
