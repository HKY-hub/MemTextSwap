#include "gti/signature_scan.h"

#include <sstream>

#include <windows.h>

namespace gti {
namespace {

bool HexNibble(char c, uint8_t* out) {
    if (c >= '0' && c <= '9') {
        *out = static_cast<uint8_t>(c - '0');
        return true;
    }
    if (c >= 'a' && c <= 'f') {
        *out = static_cast<uint8_t>(c - 'a' + 10);
        return true;
    }
    if (c >= 'A' && c <= 'F') {
        *out = static_cast<uint8_t>(c - 'A' + 10);
        return true;
    }
    return false;
}

bool ParsePattern(const std::string& hex, std::vector<uint8_t>* bytes, std::vector<bool>* mask) {
    std::string token;
    std::istringstream stream(hex);
    while (stream >> token) {
        if (token == "?" || token == "??") {
            bytes->push_back(0);
            mask->push_back(false);
            continue;
        }
        if (token.size() != 2) {
            return false;
        }
        uint8_t hi = 0, lo = 0;
        if (!HexNibble(token[0], &hi) || !HexNibble(token[1], &lo)) {
            return false;
        }
        bytes->push_back(static_cast<uint8_t>((hi << 4) | lo));
        mask->push_back(true);
    }
    return !bytes->empty();
}

uint8_t* ScanRegion(uint8_t* start, size_t size, const std::vector<uint8_t>& bytes,
                    const std::vector<bool>& mask) {
    if (bytes.empty() || bytes.size() > size) {
        return nullptr;
    }
    const size_t last = size - bytes.size();
    for (size_t i = 0; i <= last; ++i) {
        size_t j = 0;
        for (; j < bytes.size(); ++j) {
            if (mask[j] && start[i + j] != bytes[j]) {
                break;
            }
        }
        if (j == bytes.size()) {
            return start + i;
        }
    }
    return nullptr;
}

}  // namespace

uint8_t* FindFirstPattern(HMODULE module, const std::vector<Pattern>& patterns) {
    if (!module) {
        return nullptr;
    }
    auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        return nullptr;
    }
    auto nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(
        reinterpret_cast<const uint8_t*>(module) + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) {
        return nullptr;
    }
    uint8_t* base = reinterpret_cast<uint8_t*>(module);
    size_t imageSize = nt->OptionalHeader.SizeOfImage;

    MEMORY_BASIC_INFORMATION mbi{};
    uint8_t* cursor = base;
    while (cursor < base + imageSize) {
        if (VirtualQuery(cursor, &mbi, sizeof(mbi)) == 0) {
            break;
        }
        if (mbi.State == MEM_COMMIT && mbi.Protect != PAGE_NOACCESS &&
            (mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
                            PAGE_READWRITE | PAGE_READONLY)) != 0) {
            for (const auto& pattern : patterns) {
                std::vector<uint8_t> bytes;
                std::vector<bool> mask;
                if (!ParsePattern(pattern.hex, &bytes, &mask)) {
                    continue;
                }
                if (uint8_t* hit = ScanRegion(cursor, mbi.RegionSize, bytes, mask)) {
                    return hit;
                }
            }
        }
        cursor += mbi.RegionSize;
        if (mbi.RegionSize == 0) {
            break;
        }
    }
    return nullptr;
}

uint8_t* FindPatternInModule(HMODULE module, const std::string& hexPattern) {
    std::vector<uint8_t> bytes;
    std::vector<bool> mask;
    if (!ParsePattern(hexPattern, &bytes, &mask)) {
        return nullptr;
    }
    return FindFirstPattern(module, {Pattern{hexPattern}});
}

}  // namespace gti
