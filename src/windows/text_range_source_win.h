// The DirectWrite source behind the TextRangeSource seam. The macOS twin is
// the block records the mac painter already holds, which is why that painter
// asks its own layout for the rectangles instead of hit testing again.
//
// One object per block and per paint pass: it builds the block's line cache
// once, then answers every range the search and marker layers ask for from
// that cache.

#ifndef MARKDOWNIT_SRC_WINDOWS_TEXT_RANGE_SOURCE_WIN_H_
#define MARKDOWNIT_SRC_WINDOWS_TEXT_RANGE_SOURCE_WIN_H_

#include "platform/seams.h"

#include <dwrite.h>

#include <vector>

namespace platform {

class DWriteRangeSource final : public TextRangeSource {
public:
    // The origin is the top left of the block's text, in target
    // coordinates, which is where the block's layout was asked to place
    // its glyphs.
    DWriteRangeSource(IDWriteTextLayout* layout, float originX, float originY);
    // The block's own seam handle: the adapter reads the native object
    // back out of it, so a caller never names IDWriteTextLayout.
    DWriteRangeSource(const TextLayoutHandle& block, float originX,
                      float originY);
    ~DWriteRangeSource() override;

    DWriteRangeSource(const DWriteRangeSource&) = delete;
    DWriteRangeSource& operator=(const DWriteRangeSource&) = delete;

    void AppendRangeRects(uint32_t start, uint32_t end, float cullTop,
                          float cullBottom,
                          std::vector<Rect>* out) override;

private:
    // One line of the block, as DirectWrite reports it.
    struct Line {
        DWRITE_LINE_METRICS metrics;
        float top = 0.0f;    // top, relative to the block's origin
        UINT32 start = 0;    // UTF-16 index of the line's first character
    };

    // Fills the cache. A layout that reports no lines stays invalid, and
    // then every range answers nothing.
    void BuildCache();

    // Borrowed; the caller owns the layout and keeps it alive for the
    // paint pass.
    IDWriteTextLayout* layout_ = nullptr;
    float originX_ = 0.0f;
    float originY_ = 0.0f;

    std::vector<Line> lines_;
    UINT32 totalU16_ = 0;
    bool valid_ = false;
};

}  // namespace platform

#endif  // MARKDOWNIT_SRC_WINDOWS_TEXT_RANGE_SOURCE_WIN_H_
