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
    // We need to convert it to a UTF-8 source offset.
    // For now, use textStartOffset + the UTF-16 position as a first
    // approximation. The conversion helper handles multi-byte chars.
    // The layout text was built from the node's children, so the
    // textStartOffset is the source offset of the first character.
    return bl.textStartOffset + htm.textPosition;
}

bool LayoutCache::OffsetToCaretRect(uint32_t offset,
                                     float* outX, float* outY,
                                     float* outH) const {
    int idx = BlockForOffset(offset);
    if (idx < 0) return false;

    const auto& bl = blocks_[idx];
    if (!bl.layout) return false;

    // Convert source offset to UTF-16 position within this layout.
    uint32_t localOffset = (offset >= bl.textStartOffset)
        ? offset - bl.textStartOffset : 0;

    DWRITE_HIT_TEST_METRICS htm = {};
    FLOAT caretX = 0.0f, caretY = 0.0f;
    bl.layout->HitTestTextPosition(
        localOffset, FALSE, &caretX, &caretY, &htm);

    if (outX) *outX = bl.x + caretX;
    if (outY) *outY = bl.y + caretY;
    if (outH) *outH = htm.height;
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
