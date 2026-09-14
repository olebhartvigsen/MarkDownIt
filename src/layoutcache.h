#pragma once
#include <dwrite.h>
#include <vector>
#include <cstdint>
#include <string>
#include "dom.h"

// One entry per rendered block: the live layout plus the rect it
// occupied, in document coordinates (before scroll translation).
struct BlockLayout {
    IDWriteTextLayout* layout = nullptr; // owned, may be null
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    uint32_t srcOffset = 0;   // copied from Node; start of rendered content
    uint32_t srcLength = 0;
    uint32_t srcCellStart = 0; // extended start (before opening markers)
    uint32_t srcCellEnd = 0;  // extended end (past closing markers)
    uint32_t textStartOffset = 0; // source offset of layout char 0
    size_t nodeIndex = 0;
    // Maps each UTF-16 code-unit position in the layout to its
    // UTF-8 source byte offset. Built during Render() for accurate
    // hit-testing and caret placement when inline syntax is present.
    std::vector<uint32_t> u16ToSrc;
    float fontHeight = 0.0f; // em size in DIP (for caret height)
};

class LayoutCache {
public:
    ~LayoutCache();
    void Clear();
    void Add(BlockLayout bl);
    const std::vector<BlockLayout>& Blocks() const { return blocks_; }

    // Find the block whose rect contains (x, y). Returns -1 if none.
    int HitTestBlock(float x, float y) const;

    // Find the topmost block whose y-range contains y, ignoring x.
    // Used for margin-click line selection. Returns -1 if none.
    int FindBlockAtY(float y) const;

    // Find the visual line within a block at the given y coordinate.
    // Returns the source offset range [start, end) of the visual line.
    // Also outputs the line's top y (relative to block top).
    // Returns false if the block has no layout or y is out of range.
    bool GetLineRangeAtY(int blockIndex, float y,
                         uint32_t* outStart, uint32_t* outEnd,
                         float* outLineTop = nullptr) const;

    // Find the block owning a source offset. Returns -1 if none.
    int BlockForOffset(uint32_t offset) const;

    // True when the source offset maps to rendered (visible) text.
    // Offsets between blocks (blank lines, fence markers) are not
    // rendered; caret navigation should skip past them.
    bool OffsetIsRendered(uint32_t offset) const;

    // Set the source text for marker-aware offset calculations.
    void SetSourceText(const std::string* src) { srcText_ = src; }
    const std::string* SourceText() const { return srcText_; }

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
    const std::string* srcText_ = nullptr; // source text for offset calc
};

// UTF-8 / UTF-16 offset conversion helpers.
// The buffer is UTF-8, DirectWrite layouts are UTF-16. These functions
// convert between byte offsets in the UTF-8 string and code-unit offsets
// in the UTF-16 string.
uint32_t Utf8OffsetToUtf16(const std::string& s, uint32_t byteOffset);
uint32_t Utf16OffsetToUtf8(const std::string& s, uint32_t u16Offset);
