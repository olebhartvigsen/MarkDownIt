// The shared selection rectangle rule, plan Batch 3.3.
//
// Both renderers answer "which rectangles does a document range
// cover" with the same shape: one rectangle per wrapped line the range
// crosses, tight on the drawn text, inside the bands the shared walk
// laid the glyphs into. The rule lived spelled-out only in the mac
// renderer (its RectsForRange family); the Windows renderer painted
// its selection from a second, DirectWrite side path built on its own
// per-layout line extents, so the two ports could drift, and did, on
// every selection whose range crossed a block boundary (see the
// boundary clamp below).
//
// This header holds that rule once. A region is the pair the rule
// needs: the engine-laid lines of one drawing region (a text block's
// own lines, one table cell, one code card) and its rendered-to-source
// byte map. The range and the map are both in TEXT BYTE space, UTF-8
// source bytes on one side of the map and UTF-8 rendered bytes on the
// other, the seam's own unit, so nothing here converts characters. The
// geometry answers (where an offset sits on a line) come from the text
// engine seam only, which keeps this header portable: the test suites
// drive it with the fake engine on both runners.
//
// The builders of the maps live here too, because a map that disagrees
// between the ports would disagree in the washes: inline children,
// parser-mapped table cells and the "the raw text is its own source"
// rule of a code card. Both renderers call these exact functions, the
// mac one from its block adoption and the Windows one from its walk
// region mirror.

#ifndef MARKDOWNIT_SRC_LAYOUT_SELECTION_RECTS_H_
#define MARKDOWNIT_SRC_LAYOUT_SELECTION_RECTS_H_

#include "dom.h"
#include "markers.h"
#include "platform/seams.h"

#include <cstdint>
#include <string>
#include <vector>

namespace layout {

// ---------------------------------------------------------------------------
// The rendered-to-source maps
// ---------------------------------------------------------------------------

// The UTF-8 byte length of the character that starts at the byte.
inline size_t Utf8CharLen(const std::string& s, size_t at) {
    const unsigned char b = static_cast<unsigned char>(s[at]);
    if (b < 0x80) return 1;
    if ((b & 0xE0) == 0xC0) return 2;
    if ((b & 0xF0) == 0xE0) return 3;
    if ((b & 0xF8) == 0xF0) return 4;
    return 1;
}

// Close a map with the sentinel end entry, or leave it holding the one
// addressable position an empty region keeps. The sentinel makes the
// position past the last character answerable, the ends selection and
// caret queries need.
inline void FinishMapping(uint32_t emptyPosition,
                          std::vector<uint32_t>* visToSrc,
                          std::vector<uint32_t>* visToSrcEnd) {
    if (!visToSrcEnd->empty()) {
        visToSrc->push_back(visToSrcEnd->back());
        visToSrcEnd->push_back(visToSrcEnd->back());
    } else {
        visToSrc->push_back(emptyPosition);
        visToSrcEnd->push_back(emptyPosition);
    }
}

// Fully append the visible-to-source map of an inline block list. One
// entry per rendered byte, plus the end of the last character as the
// final entry. An escape or entity whose source is longer than its
// text ends where the source ends, so the caret steps over the whole
// source run, and an image child is not rendered text and maps
// nothing.
inline void AppendInlineMapping(const std::vector<InlineBlock>& children,
                                std::vector<uint32_t>* visToSrc,
                                std::vector<uint32_t>* visToSrcEnd) {
    for (const InlineBlock& child : children) {
        if (child.kind == InlineKind::Image) continue;
        if (child.text.empty()) continue;
        uint32_t srcByte = child.srcOffset;
        for (char32_t cp : child.text) {
            const uint32_t utf8Len = cp <= 0x7F ? 1u
                                    : cp <= 0x7FF ? 2u
                                    : cp <= 0xFFFF ? 3u : 4u;
            uint32_t srcEnd = srcByte + utf8Len;
            // A span the parser gave a longer source than its text (an
            // escape or an entity) ends where the source ends.
            if (child.text.size() == 1 && child.srcLength > utf8Len) {
                srcEnd = child.srcOffset + child.srcLength;
            }
            for (uint32_t k = 0; k < utf8Len; ++k) {
                visToSrc->push_back(srcByte);
                visToSrcEnd->push_back(srcEnd);
            }
            srcByte += utf8Len;
        }
    }
    if (!visToSrcEnd->empty()) {
        visToSrc->push_back(visToSrcEnd->back());
        visToSrcEnd->push_back(visToSrcEnd->back());
    }
}

// The visible-to-source map of a table cell, from the parser's own
// UTF-16-to-source mapping. The parser's mapping can hold gaps (a
// mark split into several tokens inside a cell), so a code point
// past the mapped span falls back to the walking source position.
inline void AppendCellMapping(const TableCell& cell,
                              std::vector<uint32_t>* visToSrc,
                              std::vector<uint32_t>* visToSrcEnd) {
    uint32_t u16 = 0;
    uint32_t srcByte = cell.srcOffset;
    for (char32_t cp : cell.text) {
        const uint32_t utf8Len = cp <= 0x7F ? 1u
                                : cp <= 0x7FF ? 2u
                                : cp <= 0xFFFF ? 3u : 4u;
        uint32_t start = srcByte;
        uint32_t end = srcByte + utf8Len;
        if (u16 < cell.u16ToSrc.size()) {
            start = cell.u16ToSrc[u16];
            end = u16 < cell.u16ToSrcEnd.size() ? cell.u16ToSrcEnd[u16]
                                                : start + utf8Len;
        }
        for (uint32_t k = 0; k < utf8Len; ++k) {
            visToSrc->push_back(start);
            visToSrcEnd->push_back(end);
        }
        u16 += cp > 0xFFFF ? 2u : 1u;
        srcByte = end;
    }
    FinishMapping(cell.srcOffset, visToSrc, visToSrcEnd);
}

// The visible-to-source map of a code block: the raw text is drawn as
// it stands, so the map walks the source from the content start.
inline void AppendCodeMapping(const std::string& code,
                              uint32_t contentOffset,
                              std::vector<uint32_t>* visToSrc,
                              std::vector<uint32_t>* visToSrcEnd) {
    uint32_t srcByte = contentOffset;
    for (size_t i = 0; i < code.size();) {
        const size_t len = Utf8CharLen(code, i);
        for (size_t k = 0; k < len; ++k) {
            visToSrc->push_back(srcByte);
            visToSrcEnd->push_back(srcByte +
                                   static_cast<uint32_t>(len));
        }
        srcByte += static_cast<uint32_t>(len);
        i += len;
    }
    FinishMapping(contentOffset, visToSrc, visToSrcEnd);
}

// The visible byte offset of a source position. Returns false when the
// block does not draw the position at all.
inline bool VisibleAtSource(const std::vector<uint32_t>& visToSrc,
                            uint32_t src, uint32_t* out) {
    if (visToSrc.empty()) return false;
    if (src < visToSrc.front() || src > visToSrc.back()) return false;
    const uint32_t last = static_cast<uint32_t>(visToSrc.size() - 1);
    uint32_t lo = 0;
    uint32_t hi = static_cast<uint32_t>(visToSrc.size());
    while (lo < hi) {
        const uint32_t mid = (lo + hi) / 2;
        if (visToSrc[mid] < src) lo = mid + 1;
        else hi = mid;
    }
    if (out) *out = lo > last ? last : lo;
    return true;
}

// First map index whose source byte is at or past src. The map is
// non-decreasing (the rendered text walks the source in order), so the
// binary search is exact.
inline size_t LowerBoundMapping(const std::vector<uint32_t>& visToSrc,
                                uint32_t src) {
    size_t lo = 0;
    size_t hi = visToSrc.size();
    while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2;
        if (visToSrc[mid] < src) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}

// ---------------------------------------------------------------------------
// The rectangle rule
// ---------------------------------------------------------------------------

// The rectangles a range of already-mapped visible text covers, one
// per crossed line of the region:
//
// * The x runs from the left edge of the range's first character to
//   the left edge of the character after it (the trailing edge), both
//   answered by the engine's OffsetToRect, so a wash never extends
//   past the ink its line drew.
// * The y is the line's top and the height is the line's height, the
//   same band the replay paints the glyphs of that line into.
// * A cell region shifts x by the column's alignment over the inner
//   width, the shift the painter applies to the glyphs themselves.
// * trimBlanks trims a segment to its non-blank text, which only the
//   marker washes ask for: a mark covers the drawn words, never the
//   blank beside them. The selection passes false and keeps its exact
//   range.
inline void AppendVisibleRangeRects(
    const std::vector<platform::TextLine>& lines,
    const platform::TextEngine& engine,
    float x, float top, uint32_t start, uint32_t end,
    std::vector<platform::Rect>* rects,
    ColumnAlign align = ColumnAlign::Default,
    float innerWidth = 0.0f, bool trimBlanks = false) {
    if (lines.empty()) return;
    if (end < start) {
        const uint32_t swap = start;
        start = end;
        end = swap;
    }

    float lineTop = top;
    uint32_t lineStartVis = 0;
    for (const platform::TextLine& line : lines) {
        uint32_t length = 0;
        for (const platform::GlyphRun& run : line.runs) {
            length += static_cast<uint32_t>(run.text.size());
        }
        const uint32_t lineEndVis = lineStartVis + length;

        const uint32_t from = start > lineStartVis ? start : lineStartVis;
        const uint32_t to = end < lineEndVis ? end : lineEndVis;
        // Nothing of the range reaches this line. The subtraction
        // below would wrap when the range ended before the line
        // starts, and the wrapped value reads as a range of four
        // billion bytes.
        if (from >= to) {
            lineStartVis = lineEndVis;
            lineTop += line.height;
            continue;
        }
        uint32_t localFrom = from - lineStartVis;
        uint32_t localTo = to - lineStartVis;
        if (trimBlanks && localFrom < localTo) {
            // A line's width keeps the blank beside a word, and a
            // range that ends next to the word can still reach that
            // blank. The wash covers the drawn words, so the segment
            // is trimmed to non-blank text here, where the line's own
            // bytes and their offsets are known.
            std::string lineText;
            for (const platform::GlyphRun& run : line.runs) {
                lineText += run.text;
            }
            if (localTo > lineText.size()) {
                localTo = static_cast<uint32_t>(lineText.size());
            }
            while (localFrom < localTo && IsBlankByte(lineText[localFrom])) {
                ++localFrom;
            }
            while (localTo > localFrom &&
                   IsBlankByte(lineText[localTo - 1])) {
                --localTo;
            }
        }
        if (localFrom < localTo) {
            // The line sits at the column alignment when the region
            // is a cell, the same shift the painter applies.
            float lineX = x;
            if (align == ColumnAlign::Center) {
                lineX += (innerWidth - line.width) * 0.5f;
            } else if (align == ColumnAlign::Right) {
                lineX += innerWidth - line.width;
            }
            const platform::Rect first =
                engine.OffsetToRect(line, localFrom);
            // The right edge of the range is the left edge of the
            // character after it, the rect OffsetToRect gives for that
            // position.
            const platform::Rect after =
                engine.OffsetToRect(line, localTo);
            platform::Rect rect;
            rect.x = lineX + first.x;
            rect.y = lineTop;
            rect.width = after.x - first.x;
            rect.height = line.height;
            if (rect.width < 0.0f) rect.width = 0.0f;
            rects->push_back(rect);
        }
        lineStartVis = lineEndVis;
        lineTop += line.height;
    }
}

// The rectangles a source range covers inside one region. The range is
// walked in visible space, so the rectangles land on the drawn text
// and the markers inside the range never widen it.
//
// A boundary that sits outside the region's drawn span clamps to the
// nearest drawn edge: a selection that covers a block whole still
// washes the block whole, because the wash must sit under every glyph
// the selection covers (word processor guidelines, selection and
// consistency criteria). The mac rule first dropped such regions
// outright, which meant a selection crossing block boundaries
// painted nothing at all in any block; the Windows fill clamped but
// from its own layout. Both ports clamp here now, from the one rule.
inline void AppendRangeRects(
    const std::vector<platform::TextLine>& lines,
    const std::vector<uint32_t>& visToSrc,
    const platform::TextEngine& engine,
    float x, float top, uint32_t start, uint32_t end,
    std::vector<platform::Rect>* rects,
    ColumnAlign align = ColumnAlign::Default,
    float innerWidth = 0.0f, bool trimBlanks = false) {
    if (lines.empty() || visToSrc.empty()) return;
    if (end < start) {
        const uint32_t swap = start;
        start = end;
        end = swap;
    }
    // The range misses the region entirely.
    if (start > visToSrc.back() || end < visToSrc.front()) return;

    // Each boundary maps to the first drawn byte at or past it, and a
    // boundary past the map clamps to the sentinel end entry. A
    // boundary before the map clamps to the first entry (the vector
    // only reaches a boundary outside its span when the range covers
    // the region whole, the clamp case above).
    const uint32_t last = static_cast<uint32_t>(visToSrc.size() - 1);
    size_t fromIdx = LowerBoundMapping(visToSrc, start);
    size_t toIdx = LowerBoundMapping(visToSrc, end);
    if (fromIdx > last) fromIdx = last;
    if (toIdx > last) toIdx = last;
    const uint32_t visStart = static_cast<uint32_t>(fromIdx);
    const uint32_t visEnd = static_cast<uint32_t>(toIdx);
    if (visEnd <= visStart) return;
    AppendVisibleRangeRects(lines, engine, x, top, visStart, visEnd,
                            rects, align, innerWidth, trimBlanks);
}

}  // namespace layout

#endif  // MARKDOWNIT_SRC_LAYOUT_SELECTION_RECTS_H_
