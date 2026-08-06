#include "gti/string_util.h"

#include <windows.h>

namespace gti {

std::string WidenToUtf8(const wchar_t* text, int len) {
    if (!text) {
        return {};
    }
    if (len < 0) {
        len = static_cast<int>(wcslen(text));
    }
    if (len == 0) {
        return {};
    }
    int size = WideCharToMultiByte(CP_UTF8, 0, text, len, nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        return {};
    }
    std::string out(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, len, out.data(), size, nullptr, nullptr);
    return out;
}

std::wstring Utf8ToWiden(const char* text, int len) {
    if (!text) {
        return {};
    }
    if (len < 0) {
        len = static_cast<int>(strlen(text));
    }
    if (len == 0) {
        return {};
    }
    int size = MultiByteToWideChar(CP_UTF8, 0, text, len, nullptr, 0);
    if (size <= 0) {
        return {};
    }
    std::wstring out(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text, len, out.data(), size);
    return out;
}

bool DecodeUtf8(const std::string& s, size_t* pos, uint32_t* cp) {
    const uint8_t* p = reinterpret_cast<const uint8_t*>(s.data());
    size_t i = *pos;
    if (i >= s.size()) {
        return false;
    }
    uint8_t b0 = p[i];
    if (b0 < 0x80) {
        *cp = b0;
        *pos = i + 1;
        return true;
    }
    int extra = 0;
    uint32_t code = 0;
    if ((b0 & 0xE0) == 0xC0) {
        extra = 1;
        code = b0 & 0x1F;
    } else if ((b0 & 0xF0) == 0xE0) {
        extra = 2;
        code = b0 & 0x0F;
    } else if ((b0 & 0xF8) == 0xF0) {
        extra = 3;
        code = b0 & 0x07;
    } else {
        return false;
    }
    if (i + extra >= s.size()) {
        return false;
    }
    for (int k = 1; k <= extra; ++k) {
        uint8_t b = p[i + static_cast<size_t>(k)];
        if ((b & 0xC0) != 0x80) {
            return false;
        }
        code = (code << 6) | (b & 0x3F);
    }
    *cp = code;
    *pos = i + static_cast<size_t>(extra) + 1;
    return true;
}

bool HasCjkUtf8(const std::string& s) {
    size_t pos = 0;
    while (pos < s.size()) {
        uint32_t cp = 0;
        if (!DecodeUtf8(s, &pos, &cp)) {
            return false;
        }
        if ((cp >= 0x4E00 && cp <= 0x9FFF) || (cp >= 0x3400 && cp <= 0x4DBF) ||
            (cp >= 0xF900 && cp <= 0xFAFF) || (cp >= 0xFF00 && cp <= 0xFFEF) ||
            (cp >= 0x3000 && cp <= 0x303F)) {
            return true;
        }
    }
    return false;
}

bool HasAsciiLetter(const std::string& s) {
    for (unsigned char c : s) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
            return true;
        }
    }
    return false;
}

bool LooksLikePathOrUrl(const std::string& s) {
    return s.find("://") != std::string::npos || s.find('\\') != std::string::npos ||
           s.find("C:") != std::string::npos || s.find("/") != std::string::npos;
}

bool IsMostlyAsciiControl(const std::string& s) {
    size_t control = 0;
    for (unsigned char c : s) {
        if (c < 0x20) {
            ++control;
        }
    }
    return !s.empty() && control * 2 >= s.size();
}

}  // namespace gti
