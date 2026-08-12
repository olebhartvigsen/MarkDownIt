#include "navigation.h"
#include "editcontroller.h"

uint32_t MoveLeft(const TextBuffer& buf, uint32_t offset) {
    return PrevGraphemeBoundary(buf.Text(), offset);
}

uint32_t MoveRight(const TextBuffer& buf, uint32_t offset) {
    return NextGraphemeBoundary(buf.Text(), offset);
}

static bool isWordChar(unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_';
}

uint32_t MoveWordLeft(const TextBuffer& buf, uint32_t offset) {
    const std::string& s = buf.Text();
    if (offset == 0) return 0;
    uint32_t i = offset;
    // Skip whitespace backward.
    while (i > 0 && isspace(static_cast<unsigned char>(s[i - 1]))) i--;
    // Skip word characters backward.
    if (i > 0 && isWordChar(static_cast<unsigned char>(s[i - 1]))) {
        while (i > 0 && isWordChar(static_cast<unsigned char>(s[i - 1]))) i--;
    } else {
        // Skip non-word, non-space characters backward (punctuation).
        while (i > 0 && !isspace(static_cast<unsigned char>(s[i - 1])) &&
               !isWordChar(static_cast<unsigned char>(s[i - 1]))) i--;
    }
    return i;
}

uint32_t MoveWordRight(const TextBuffer& buf, uint32_t offset) {
    const std::string& s = buf.Text();
    uint32_t n = static_cast<uint32_t>(s.size());
    if (offset >= n) return n;
    uint32_t i = offset;
    // Skip whitespace forward.
    while (i < n && isspace(static_cast<unsigned char>(s[i]))) i++;
    // Skip word characters forward.
    if (i < n && isWordChar(static_cast<unsigned char>(s[i]))) {
        while (i < n && isWordChar(static_cast<unsigned char>(s[i]))) i++;
    } else {
        // Skip non-word, non-space characters forward.
        while (i < n && !isspace(static_cast<unsigned char>(s[i])) &&
               !isWordChar(static_cast<unsigned char>(s[i]))) i++;
    }
    return i;
}

uint32_t MoveVertical(const LayoutCache& lc, uint32_t offset,
                      int direction, float* desiredX,
                      float scrollY, float lineHeight) {
    float x, y, h;
    if (!lc.OffsetToCaretRect(offset, &x, &y, &h)) return offset;

    if (*desiredX < 0) *desiredX = x;

    float targetY = y + direction * lineHeight;
    uint32_t newOffset = lc.PointToOffset(*desiredX, targetY);
    if (newOffset == UINT32_MAX) return offset;
    return newOffset;
}

uint32_t MoveLineStart(const LayoutCache& lc, uint32_t offset) {
    float x, y, h;
    if (!lc.OffsetToCaretRect(offset, &x, &y, &h)) return offset;
    // Hit test at the leftmost edge of this block.
    int blockIdx = lc.BlockForOffset(offset);
    if (blockIdx < 0) return offset;
    const auto& bl = lc.Blocks()[blockIdx];
    uint32_t newOffset = lc.PointToOffset(bl.x, y);
    if (newOffset == UINT32_MAX) return offset;
    return newOffset;
}

uint32_t MoveLineEnd(const LayoutCache& lc, uint32_t offset) {
    float x, y, h;
    if (!lc.OffsetToCaretRect(offset, &x, &y, &h)) return offset;
    int blockIdx = lc.BlockForOffset(offset);
    if (blockIdx < 0) return offset;
    const auto& bl = lc.Blocks()[blockIdx];
    uint32_t newOffset = lc.PointToOffset(bl.x + bl.width - 1, y);
    if (newOffset == UINT32_MAX) return offset;
    return newOffset;
}
