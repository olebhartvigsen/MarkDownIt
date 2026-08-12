#include "layoutcache.h"

LayoutCache::~LayoutCache() {
    Clear();
}

void LayoutCache::Clear() {
    for (auto& bl : blocks_) {
        if (bl.layout) {
            bl.layout->Release();
            bl.layout = nullptr;
        }
    }
    blocks_.clear();
}

void LayoutCache::Add(BlockLayout bl) {
    blocks_.push_back(bl);
}

int LayoutCache::HitTestBlock(float x, float y) const {
    for (size_t i = 0; i < blocks_.size(); ++i) {
        const auto& bl = blocks_[i];
        if (x >= bl.x && x < bl.x + bl.width &&
            y >= bl.y && y < bl.y + bl.height) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int LayoutCache::BlockForOffset(uint32_t offset) const {
    int best = -1;
    uint32_t bestLen = 0xFFFFFFFF;
    for (size_t i = 0; i < blocks_.size(); ++i) {
        const auto& bl = blocks_[i];
        if (offset >= bl.srcOffset && offset <= bl.srcOffset + bl.srcLength) {
            uint32_t len = bl.srcLength;
            if (len < bestLen) {
                bestLen = len;
                best = static_cast<int>(i);
            }
        }
    }
    return best;
}

uint32_t LayoutCache::PointToOffset(float x, float y) const {
    int idx = HitTestBlock(x, y);
    if (idx < 0) return UINT32_MAX;

    const auto& bl = blocks_[idx];
    if (!bl.layout) return bl.textStartOffset;

    // Convert screen coordinates to layout-local coordinates.
    float localX = x - bl.x;
    float localY = y - bl.y;

    DWRITE_HIT_TEST_METRICS htm = {};
    BOOL isTrailingHit = FALSE;
    BOOL isInside = FALSE;
    bl.layout->HitTestPoint(
        localX, localY, &isTrailingHit, &isInside, &htm);

    // htm.textPosition is a UTF-16 code-unit position within the layout.
    // Use the u16ToSrc mapping to convert to a UTF-8 source offset.
    if (!bl.u16ToSrc.empty() && htm.textPosition < bl.u16ToSrc.size()) {
        uint32_t srcOff = bl.u16ToSrc[htm.textPosition];
        if (isTrailingHit && htm.textPosition + 1 < bl.u16ToSrc.size()) {
            // For trailing hits, advance to the next source position.
            // But only if the next position is within the same inline block.
            // Use the next mapped position if it's > current.
            uint32_t nextOff = bl.u16ToSrc[htm.textPosition + 1];
            if (nextOff > srcOff) return nextOff;
        }
        return srcOff;
    }
    // Fallback: linear approximation (for blocks without mapping).
    return bl.textStartOffset + htm.textPosition;
}

bool LayoutCache::OffsetToCaretRect(uint32_t offset,
                                     float* outX, float* outY,
                                     float* outH) const {
    int idx = BlockForOffset(offset);
    if (idx < 0) return false;

    const auto& bl = blocks_[idx];
    if (!bl.layout) return false;

    // Convert source offset to UTF-16 position within this layout
    // using the u16ToSrc mapping (reverse lookup).
    uint32_t u16Pos = 0;
    if (!bl.u16ToSrc.empty()) {
        // Binary search for the first u16 index whose src offset >= offset.
        size_t lo = 0, hi = bl.u16ToSrc.size();
        while (lo < hi) {
            size_t mid = (lo + hi) / 2;
            if (bl.u16ToSrc[mid] < offset) lo = mid + 1;
            else hi = mid;
        }
        u16Pos = (lo < bl.u16ToSrc.size())
            ? static_cast<uint32_t>(lo)
            : static_cast<uint32_t>(bl.u16ToSrc.size());
    } else {
        // Fallback: linear approximation.
        u16Pos = (offset >= bl.textStartOffset)
            ? offset - bl.textStartOffset : 0;
    }

    DWRITE_HIT_TEST_METRICS htm = {};
    FLOAT caretX = 0.0f, caretY = 0.0f;
    bl.layout->HitTestTextPosition(
        u16Pos, FALSE, &caretX, &caretY, &htm);

    if (outX) *outX = bl.x + caretX;
    // Vertically center the caret on the text within the line.
    // caretY is the top of the line; the text occupies roughly the
    // em-height portion, centered by the line-spacing/baseline ratio.
    if (outY) {
        float caretH = (bl.fontHeight > 0.0f) ? bl.fontHeight : htm.height;
        float lineH = htm.height;
        *outY = bl.y + caretY + (lineH - caretH) * 0.5f;
    }
    // Use the font's em height for the caret, not the full line height
    // (which includes leading/line-spacing and makes the caret too tall).
    if (outH) *outH = (bl.fontHeight > 0.0f) ? bl.fontHeight : htm.height;
    return true;
}

// --- UTF-8 / UTF-16 conversion helpers ---

// Count UTF-16 code units needed for the first byteOffset bytes of s.
uint32_t Utf8OffsetToUtf16(const std::string& s, uint32_t byteOffset) {
    uint32_t u16 = 0;
    uint32_t i = 0;
    while (i < byteOffset && i < s.size()) {
        unsigned char b = static_cast<unsigned char>(s[i]);
        if (b < 0x80) {
            i += 1;
        } else if ((b & 0xE0) == 0xC0) {
            i += 2;
        } else if ((b & 0xF0) == 0xE0) {
            i += 3;
        } else if ((b & 0xF8) == 0xF0) {
            // Surrogate pair: 4 UTF-8 bytes -> 2 UTF-16 code units.
            i += 4;
            u16++;  // account for the extra UTF-16 unit
        } else {
            i += 1;
        }
        u16++;
    }
    return u16;
}

// Convert a UTF-16 code-unit offset back to a UTF-8 byte offset in s.
uint32_t Utf16OffsetToUtf8(const std::string& s, uint32_t u16Offset) {
    uint32_t u16 = 0;
    uint32_t i = 0;
    while (u16 < u16Offset && i < s.size()) {
        unsigned char b = static_cast<unsigned char>(s[i]);
        if (b < 0x80) {
            i += 1;
        } else if ((b & 0xE0) == 0xC0) {
            i += 2;
        } else if ((b & 0xF0) == 0xE0) {
            i += 3;
        } else if ((b & 0xF8) == 0xF0) {
            i += 4;
            u16++;  // surrogate pair: 2 UTF-16 units
        } else {
            i += 1;
        }
        u16++;
    }
    return i;
}
