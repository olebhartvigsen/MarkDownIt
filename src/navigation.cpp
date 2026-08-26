#include "navigation.h"
#include "editcontroller.h"

// Check if a source offset is a hidden markdown marker character
// (not part of the rendered text) using the layout cache's u16ToSrc
// mapping. If the offset is not in any block's u16ToSrc, it's hidden.
static bool isHiddenMarker(const std::string& s, uint32_t offset,
                           const LayoutCache* cache) {
    if (!cache) return false;
    // Quick check: is the char a markdown marker?
    if (offset >= s.size()) return false;
    char c = s[offset];
    if (c != '*' && c != '`' && c != '~') return false;
    // Check if this offset is in the rendered text of any block.
    // The u16ToSrc vector maps rendered UTF-16 positions to source
    // byte offsets. If offset appears in any block's u16ToSrc, it's
    // rendered (visible). Otherwise it's a hidden marker.
    const auto& blocks = cache->Blocks();
    for (const auto& bl : blocks) {
        for (uint32_t srcOff : bl.u16ToSrc) {
            if (srcOff == offset) return false;  // rendered → not hidden
            if (srcOff > offset) break;          // sorted, no point
        }
    }
    return true;  // marker char not found in any rendered text
}

// Skip forward past hidden marker characters starting at offset.
static uint32_t skipMarkersRight(const std::string& s, uint32_t offset,
                                 const LayoutCache* cache) {
    while (offset < s.size() && isHiddenMarker(s, offset, cache))
        offset = NextGraphemeBoundary(s, offset);
    return offset;
}

// Skip backward past hidden marker characters before offset.
static uint32_t skipMarkersLeft(const std::string& s, uint32_t offset,
                                const LayoutCache* cache) {
    while (offset > 0) {
        uint32_t prev = PrevGraphemeBoundary(s, offset);
        if (prev == offset) break;
        if (!isHiddenMarker(s, prev, cache)) break;
        offset = prev;
    }
    return offset;
}

uint32_t MoveLeft(const TextBuffer& buf, uint32_t offset,
                  const LayoutCache* layoutCache) {
    uint32_t prev = PrevGraphemeBoundary(buf.Text(), offset);
    // If we landed on a hidden marker, keep skipping left past markers.
    if (layoutCache)
        prev = skipMarkersLeft(buf.Text(), prev, layoutCache);
    // Skip over unrendered gap bytes (blank lines between blocks,
    // code-fence lines) so the caret always lands on visible text.
    if (layoutCache) {
        while (prev > 0 && !layoutCache->OffsetIsRendered(prev)) {
            uint32_t p = PrevGraphemeBoundary(buf.Text(), prev);
            if (p == prev) break;
            prev = skipMarkersLeft(buf.Text(), p, layoutCache);
        }
    }
    return prev;
}

uint32_t MoveRight(const TextBuffer& buf, uint32_t offset,
                   const LayoutCache* layoutCache) {
    const std::string& s = buf.Text();
    uint32_t next = NextGraphemeBoundary(s, offset);
    // If we landed on a hidden marker, keep skipping right past markers.
    if (layoutCache)
        next = skipMarkersRight(s, next, layoutCache);
    // Skip over unrendered gap bytes (blank lines between blocks,
    // code-fence lines) so the caret always lands on visible text.
    if (layoutCache) {
        while (next < s.size() && !layoutCache->OffsetIsRendered(next)) {
            uint32_t n = NextGraphemeBoundary(s, next);
            if (n == next) break;
            next = skipMarkersRight(s, n, layoutCache);
        }
    }
    return next;
}

static bool isWordChar(unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_' ||
           // Multi-byte UTF-8 sequences (æøå, CJK, etc.): treat the lead
           // byte and all continuation bytes as word characters so
           // word motion does not stop inside accented words.
           c >= 0x80;
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

    // Blocks can be separated by large vertical gaps (blank lines,
    // heading spacing). One lineHeight step may land between blocks
    // where PointToOffset fails; progressively scan further in the
    // move direction until we hit rendered text or run out of doc.
    uint32_t newOffset = UINT32_MAX;
    float targetY = y;
    for (int step = 1; step <= 12; ++step) {
        targetY += static_cast<float>(direction) * lineHeight;
        uint32_t cand = lc.PointToOffset(*desiredX, targetY);
        if (cand == UINT32_MAX) continue;
        // Reject hits that are still on the current visual line. A
        // tall block (H1-H3 use much larger fonts than body text)
        // makes one generic lineHeight step land back inside the
        // same block; moving must actually leave the current line.
        float cx, cy, ch;
        if (!lc.OffsetToCaretRect(cand, &cx, &cy, &ch)) continue;
        bool movedPast = (direction < 0)
            ? (cy < y - h * 0.5f)
            : (cy > y + h * 0.5f);
        if (!movedPast) continue;
        newOffset = cand;
        break;
    }
    // No block found in that direction: clamp to doc start/end so the
    // caret still moves to the first/last line of the document.
    if (newOffset == UINT32_MAX) {
        const auto& blocks = lc.Blocks();
        if (blocks.empty()) return offset;
        if (direction < 0) {
            newOffset = blocks.front().srcOffset;
        } else {
            const auto& last = blocks.back();
            // Prefer the end of the last rendered character; fall back
            // to the block's source end when there is no mapping.
            newOffset = last.u16ToSrc.empty()
                ? last.srcOffset + last.srcLength
                : last.u16ToSrc.back() + 1;
        }
    }
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
