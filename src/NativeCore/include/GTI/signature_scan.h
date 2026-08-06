#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <windows.h>

namespace gti {

struct Pattern {
    std::string hex;  // hex bytes, '?' = wildcard, e.g. "48 8B C3 90"
};

// Scan a module's executable/readable committed memory for the first pattern hit.
uint8_t* FindPatternInModule(HMODULE module, const std::string& hexPattern);
uint8_t* FindFirstPattern(HMODULE module, const std::vector<Pattern>& patterns);

}  // namespace gti
