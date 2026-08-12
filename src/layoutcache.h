#pragma once
#include <dwrite.h>
#include <vector>
#include <cstdint>
#include <string>
#include "dom.h"

// One entry per rendered block: the live layout plus the rect it
// occupied, in document coordinates (before scroll translation).
struct BlockLayout {
    IDWriteTextLayout* layout = nullptr;  // owned, may be null
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    uint32_t srcOffset = 0;    // copied from Node
    uint32_t srcLength = 0;
    uint32_t textStartOffset = 0;  // source offset of layout char 0
    size_t nodeIndex = 0;
    // Maps each UTF-16 code-unit position in the layout to its
    // UTF-8 source byte offset. Built during Render() for accurate
    // hit-testing and caret placement when inline syntax is present.
    std::vector<uint32_t> u16ToSrc;
    float fontHeight = 0.0f;  // em size in DIP (for caret height)
};

class LayoutCache {
public:
    ~LayoutCache();
    void Clear();
    void Add(BlockLayout bl);
    const std::vector<BlockLayout>& Blocks() const { return blocks_; }

    // Find the block whose rect contains (x, y). Returns -1 if none.
    int HitTestBlock(float x, float y) const;

    // Find the block owning a source offset. Returns -1 if none.
    int BlockForOffset(uint32_t offset) const;

    // Screen point to source offset. Used for mouse clicks.
    // Returns UINT32_MAX on failure.
    uint32_t PointToOffset(float x, float y) const;

    // Source offset to a caret rectangle. Used to place the caret.
    // Returns false if no block owns the offset.
    bool OffsetToCaretRect(uint32_t offset,
                            float* outX, float* outY,
                            float* outH) const;

private:
    std::vector<BlockLayout> blocks_;
};

// UTF-8 / UTF-16 offset conversion helpers.
// The buffer is UTF-8, DirectWrite layouts are UTF-16. These functions
// convert between byte offsets in the UTF-8 string and code-unit offsets
// in the UTF-16 string.
uint32_t Utf8OffsetToUtf16(const std::string& s, uint32_t byteOffset);
uint32_t Utf16OffsetToUtf8(const std::string& s, uint32_t u16Offset);
