#include "textdrag.h"
#include "editcontroller.h"
#include <algorithm>
#include <string>
#include <utility>

namespace {
bool IsContinuation(unsigned char c) { return (c & 0xC0u) == 0x80u; }

bool IsUtf8Boundary(const std::string& text, uint32_t offset) {
    return offset == 0 || offset >= text.size() ||
           !IsContinuation(static_cast<unsigned char>(text[offset]));
}

bool IsSafeBoundary(const std::string& text, uint32_t offset) {
    return IsUtf8Boundary(text, offset) && ::IsGraphemeBoundary(text, offset);
}
}

bool MoveTextRange(const std::string& text, uint32_t start, uint32_t length,
                   uint32_t drop, std::string* moved, uint32_t* newStart) {
    if (!moved || !newStart || start > text.size()) return false;
    length = std::min<uint32_t>(length,
        static_cast<uint32_t>(text.size()) - start);
    uint32_t end = start + length;
    if (length == 0 || drop > text.size() ||
        (drop >= start && drop <= end)) return false;
    if (!IsSafeBoundary(text, start) || !IsSafeBoundary(text, end) ||
        !IsSafeBoundary(text, drop)) return false;

    std::string selected = text.substr(start, length);
    std::string remaining = text;
    remaining.erase(start, length);
    uint32_t insertion = drop;
    if (drop > end) insertion -= length;
    if (insertion > remaining.size())
        insertion = static_cast<uint32_t>(remaining.size());
    remaining.insert(insertion, selected);
    *moved = std::move(remaining);
    *newStart = insertion;
    return true;
}
