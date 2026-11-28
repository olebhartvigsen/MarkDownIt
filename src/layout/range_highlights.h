#pragma once

// Filling the search-match and marker rectangles of one block of text.
//
// The pass used to be written twice inside the Windows painter, typed on
// Direct2D, with the mapping, the viewport skip and the "is this block
// touched at all" probe copied from one body into the other. It lives here
// once: the renderer hands in the ranges, a platform::TextRangeSource answers
// where they sit on the page, and the target fills what comes back. Nothing
// in this file names a platform.
//
// The order of the fills is by colour, not by range: find matches never
// overlap, so one batch per colour draws the same pixels as the interleaved
// loop the painter used to run, and a batch costs one brush instead of one
// per rectangle.

#include <cstdint>
#include <vector>

#include "platform/seams.h"
#include "searchreplace.h"  // TextMatch

namespace layout {

// One block of text: where it sits in the document, and how its bytes map to
// the UTF-16 units the platform measured.
//
// u16ToSrc[i] is the source byte offset of UTF-16 index i. The mapping is not
// guaranteed to be sorted (a table cell carries gaps where md4c split the text
// at a mark or an HTML tag), so the lookup is a binary search when it is
// non-decreasing and the old linear scan otherwise.
struct BlockTextMap {
    const std::vector<uint32_t>* u16ToSrc = nullptr;
    uint32_t u16Len = 0;
    uint32_t blockStart = 0;
    uint32_t blockEnd = 0;
};

// The vertical slice of the page a paint pass has to cover, in target
// coordinates. A range outside it costs no geometry.
struct PaintBand {
    float top = 0.0f;
    float bottom = 0.0f;
};

// True when any range overlaps the block. A block nothing touches skips the
// line cache and every hit test, which is what keeps a one character search in
// a big document cheap: most blocks are not touched at all.
bool BlockIntersectsAny(const std::vector<TextMatch>& ranges,
                        const BlockTextMap& map);

// Fills the search matches that fall inside the block: `match` for every one,
// `currentMatch` for the match at index `current` (-1 when there is none). A
// match is skipped rather than clipped when the block does not cover it, so
// two adjacent blocks never draw the same match twice.
void PaintMatchHighlights(platform::RenderTarget& target,
                          platform::TextRangeSource& source,
                          const BlockTextMap& map, const PaintBand& band,
                          const std::vector<TextMatch>& matches, int current,
                          const platform::Color& match,
                          const platform::Color& currentMatch);

// Fills the marker ranges for the block, then a two point line along the
// bottom of every rectangle so a mark does not rely on colour alone. The
// fills are trimmed to the drawn words, the rule the mac painter always
// kept and now the one both ports ask the source to apply.
void PaintMarkerHighlights(platform::RenderTarget& target,
                           platform::TextRangeSource& source,
                           const BlockTextMap& map, const PaintBand& band,
                           const std::vector<TextMatch>& markers,
                           const platform::Color& fill,
                           const platform::Color& line);

}  // namespace layout
