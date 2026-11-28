#pragma once
#include <vector>
#include <cstdint>
#include <memory>
#include <string>
#include "dom.h"

namespace platform {
class TextLayout;
}

// One entry per rendered block: the live layout plus the rect it
// occupied, in document coordinates (before scroll translation).
struct BlockLayout {
    // The block's laid out text, owned by the cache, may be null. The
    // handle is the seam's: the caret's questions are asked of it, and
    // which platform produced it is not this file's business.
    std::shared_ptr<const platform::TextLayout> layout;
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    // Where the block's own track begins, when it is not its text's left
    // edge: a list item's margin. A point left of it is not a caret.
    // Negative means "the same as x".
    float trackX = -1.0f;
    uint32_t srcOffset = 0;   // copied from Node; start of rendered content
    uint32_t srcLength = 0;
    uint32_t srcCellStart = 0; // extended start (before opening markers)
    uint32_t srcCellEnd = 0;  // extended end (past closing markers)
    uint32_t textStartOffset = 0; // source offset of layout char 0
    size_t nodeIndex = 0;
    bool isTableCell = false; // renderer sets this for table cell blocks
    // Maps each UTF-16 code-unit position in the layout to its
    // UTF-8 source byte offset. Built during Render() for accurate
    // hit-testing and caret placement when inline syntax is present.
    std::vector<uint32_t> u16ToSrc;
    std::vector<uint32_t> u16ToSrcEnd;
    float fontHeight = 0.0f; // natural font line height in DIP
                              // (ascent+descent+lineGap, zoom-scaled);
                              // caret height per cursor-blinking guide
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

    // Among blocks sharing the y band, the one horizontally nearest
    // to x. Keeps cell padding and side whitespace in the correct
    // column for parallel table cells. Returns -1 if the band is empty.
    int FindNearestBlockInRow(float x, float y) const;

    // Find the visual line within a block at the given y coordinate.
    // Returns the source offset range [start, end) of the visual line.
    // Also outputs the line's top y (relative to block top).
    // Returns false if the block has no layout or y is out of range.
    bool GetLineRangeAtY(int blockIndex, float y,
                         uint32_t* outStart, uint32_t* outEnd,
                         float* outLineTop = nullptr) const;

    bool GetRenderedBlockRange(int blockIndex,
                               uint32_t* outStart,
                               uint32_t* outEnd) const;

    uint32_t NormalizeToRenderedCaret(uint32_t offset) const;

    // Find the block owning a source offset. Returns -1 if none.
    int BlockForOffset(uint32_t offset) const;

    // True when the source offset maps to rendered (visible) text.
    // Offsets between blocks (blank lines, fence markers) are not
    // rendered; caret navigation should skip past them.
    bool OffsetIsRendered(uint32_t offset) const;

    // True when every UTF-8 code point in [start, start + length) is
    // represented by rendered text. Hidden Markdown markers are excluded.
    bool RangeIsRendered(uint32_t start, uint32_t length) const;

    // Tighten [start, start+length) to the sub-range whose bytes are all
    // rendered text. Returns false when nothing in the range is rendered.
    // The clip is safe to use for annotations: it never maps a byte that
    // the renderer does not draw.
    bool ClipToRendered(uint32_t start, uint32_t length,
                        uint32_t* outStart, uint32_t* outEnd) const;

    // Set the source text for marker-aware offset calculations.
    void SetSourceText(const std::string* src) { srcText_ = src; }
    const std::string* SourceText() const { return srcText_; }

    // Which line a caret at a soft wrap is drawn on. The position is the
    // same source byte at the end of the upper line and at the start of
    // the lower one, so the model has to be told which side the reader
    // put it on: the default draws the lower line, and a caller that
    // stepped down into the wrap sets false so the caret stays at the
    // end of the line above. The answer is exact for a layout that
    // keeps its line records (the seam's portable block, which both
    // ports build blocks with); a native block answers its own side and
    // the flag only moves the vertical step.
    void SetCaretOnLowerLine(bool lower) { caretOnLowerLine_ = lower; }
    bool CaretOnLowerLine() const { return caretOnLowerLine_; }

    // True when the screen draws the Markdown syntax itself, as the
    // source view does. A caret at the visual end of emphasis or a link
    // then stays where it is instead of stepping over the hidden
    // markers, because those markers are on screen and are positions of
    // their own.
    void SetSyntaxRevealed(bool revealed) { syntaxRevealed_ = revealed; }
    bool SyntaxRevealed() const { return syntaxRevealed_; }

    // Screen point to source offset. Used for mouse clicks.
    // Returns UINT32_MAX on failure.
    uint32_t PointToOffset(float x, float y) const;

    // Screen point to a caret offset in the block at y, including the
    // whitespace to the right of rendered text. Left-margin clicks still
    // return UINT32_MAX so line selection keeps its Word-like behavior.
    uint32_t PointToOffsetAtOrAfterBlock(float x, float y) const;

    // Source offset to a caret rectangle. Used to place the caret.
    // Returns false if no block owns the offset.
    bool OffsetToCaretRect(uint32_t offset,
                            float* outX, float* outY,
                            float* outH) const;

    // The same, for one named block. A caller that resolved the block
    // itself (the last of two adjacent cells wins a position on their
    // seam) answers from that block instead of the first one found.
    bool OffsetToCaretRectIn(int blockIndex, uint32_t offset,
                              float* outX, float* outY,
                              float* outH) const;

    // Screen point to a caret offset, landing on the nearest drawn line
    // when the point is in a gap between blocks: an exact band beats a
    // near one, an x inside the block beats one outside it, and a tie
    // goes to the line below, because a point between two bands is on
    // its way down the page. A point left of the text column is not a
    // caret (the caller keeps its line selection), and the answer is
    // not normalized. Returns UINT32_MAX when nothing is near.
    uint32_t PointToOffsetNearestLine(float x, float y) const;

    // One grapheme cluster to the left or right through the drawn text.
    // A hidden marker is never a caret position, so the step crosses the
    // markers around it, and a table cell is a wall: a step never
    // leaves the cell it starts in. Returns the offset unchanged at the
    // drawn text's own edge.
    uint32_t StepRendered(uint32_t offset, bool left) const;

private:
    uint32_t PointToOffsetInBlock(int blockIndex, float x, float y) const;

    // Coverage index behind RangeIsRendered. Collects every rendered span
    // (start, end) from every block's mapping, packed as (start << 32 | end)
    // and kept sorted, so a range test is a binary search per byte instead
    // of a scan of all blocks. Rebuilt lazily after Clear()/Add(); when the
    // spans overlap each other the index is refused and the linear scan
    // stays, because which span a byte belongs to would then depend on
    // block order.
    void EnsureSpanIndex() const;
    mutable std::vector<uint64_t> spanIndex_;
    mutable bool spanIndexBuilt_ = false;
    mutable bool spanIndexUsable_ = false;

    std::vector<BlockLayout> blocks_;
    const std::string* srcText_ = nullptr; // source text for offset calc
    bool caretOnLowerLine_ = true;
    bool syntaxRevealed_ = false;
};

// UTF-8 / UTF-16 offset conversion helpers.
// The buffer is UTF-8, the text layouts are UTF-16. These functions
// convert between byte offsets in the UTF-8 string and code-unit offsets
// in the UTF-16 string.
uint32_t Utf8OffsetToUtf16(const std::string& s, uint32_t byteOffset);
uint32_t Utf16OffsetToUtf8(const std::string& s, uint32_t u16Offset);
