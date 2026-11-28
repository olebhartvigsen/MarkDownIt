#pragma once

// One caret record, built from the block's drawn lines.
//
// The caret seam speaks UTF-16 code units, and the layout cache keeps one
// BlockLayout per drawn block and per table cell. Both ports have those
// three facts: the lines the walk measured, the region's rendered byte to
// source map, and the region's geometry. This builder turns them into the
// entry, so a caret record has one shape and one filler on both ports
// instead of one per renderer.
//
// The index space is the one the seam speaks: each code point of the drawn
// text takes one unit, or two when it is a surrogate pair, and its source
// bytes come from the region's own rendered byte map. Converting here,
// once per layout, is what keeps every caret question on the shared model.
// A region with no drawn text keeps one addressable position, the sentinel
// the shared caret model parks a caret at.

#include <cstdint>
#include <string>
#include <vector>

#include "dom.h"
#include "layout/selection_rects.h"
#include "layoutcache.h"
#include "platform/seams.h"

namespace caret {

// The height of a region: the sum of its line boxes, or the empty height
// when it draws no line at all (a cell keeps its row's height).
inline float LinesHeight(const std::vector<platform::TextLine>& lines,
                         float emptyHeight) {
    float height = 0.0f;
    for (const platform::TextLine& line : lines) height += line.height;
    return height > 0.0f ? height : emptyHeight;
}

// The record of one drawn region. `height` is used as given (`emptyHeight`
// at the call site: the row height of a cell that draws nothing, zero for
// a text block). `visToSrc` and `visToSrcEnd` are indexed by the region's
// rendered UTF-8 bytes; a short or missing entry falls back to the start
// of the region.
inline BlockLayout Record(const std::vector<platform::TextLine>& lines,
                          float x, float y, float width, float height,
                          uint32_t srcOffset, uint32_t srcLength,
                          uint32_t srcCellStart, uint32_t srcCellEnd,
                          bool isTableCell, ColumnAlign align, float trackX,
                          size_t nodeIndex, float fontHeight,
                          const std::vector<uint32_t>& visToSrc,
                          const std::vector<uint32_t>& visToSrcEnd) {
    BlockLayout bl;
    bl.x = x;
    bl.y = y;
    bl.width = width;
    bl.height = height;
    bl.srcOffset = srcOffset;
    bl.srcLength = srcLength;
    bl.textStartOffset = srcOffset;
    bl.srcCellStart = srcCellStart;
    bl.srcCellEnd = srcCellEnd;
    bl.isTableCell = isTableCell;
    bl.trackX = trackX;
    bl.nodeIndex = nodeIndex;
    bl.fontHeight = fontHeight;

    if (!lines.empty()) {
        platform::TextAlign textAlign = platform::TextAlign::Left;
        if (align == ColumnAlign::Center) {
            textAlign = platform::TextAlign::Center;
        } else if (align == ColumnAlign::Right) {
            textAlign = platform::TextAlign::Right;
        }
        bl.layout = platform::MakeLineLayout(lines, width, textAlign);

        uint32_t vis = 0;
        for (const platform::TextLine& line : lines) {
            for (const platform::GlyphRun& run : line.runs) {
                const std::string& text = run.text;
                for (size_t i = 0; i < text.size();) {
                    const size_t len = layout::Utf8CharLen(text, i);
                    uint32_t start = srcOffset;
                    uint32_t end = srcOffset;
                    if (vis < visToSrc.size()) {
                        start = visToSrc[vis];
                        end = vis < visToSrcEnd.size()
                            ? visToSrcEnd[vis]
                            : start + static_cast<uint32_t>(len);
                    }
                    const uint32_t units = len == 4 ? 2u : 1u;
                    for (uint32_t k = 0; k < units; ++k) {
                        bl.u16ToSrc.push_back(start);
                        bl.u16ToSrcEnd.push_back(end);
                    }
                    vis += static_cast<uint32_t>(len);
                    i += len;
                }
            }
        }
    }

    // A region with no drawn text keeps one addressable position, the
    // sentinel the shared caret model parks a caret at.
    if (bl.u16ToSrc.empty()) {
        bl.u16ToSrc.push_back(srcOffset);
        bl.u16ToSrcEnd.push_back(srcOffset);
    }
    return bl;
}

}  // namespace caret
