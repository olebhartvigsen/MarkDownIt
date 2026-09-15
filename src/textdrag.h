#pragma once
#include <cstdint>
#include <string>

// Move one source range to a drop position. The drop position is measured in
// the original string. Drops inside the selected range are rejected.
// newStart receives the range start in the returned string.
bool MoveTextRange(const std::string& text, uint32_t start, uint32_t length,
                   uint32_t drop, std::string* moved, uint32_t* newStart);
