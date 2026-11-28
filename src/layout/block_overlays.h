// The layers an overlay paints above a replayed card or table.
//
// The display list carries the card, the table's stripes and its grid. The
// washes cannot ride that list: a marker or a search match is a document
// range, and where its glyphs ended up is the same question the caret asks,
// which only the platform's text record can answer. So both ports replay the
// surface first and then paint the wash layer above it, and this header is
// that layer: one region (the block's own text, or one table cell's), one
// map, the marker pass and then the search pass.

#pragma once

#include "layout/range_highlights.h"
#include "platform/seams.h"
#include "searchreplace.h"  // TextMatch

#include <cstdint>
#include <vector>

namespace layout {

// One region of a block that an overlay paints above its replayed surface:
// the block's own text for a code card, or one table cell's words.
struct OverlayRegion {
    // The map the shared pass reads, index to source byte. Windows hands
    // UTF-16 units, the mac rendered bytes; the pass only needs an index
    // and the source byte it maps to, so the two never disagree with the
    // record that answered the geometry.
    const std::vector<uint32_t>* indexToSrc = nullptr;
    uint32_t indexLen = 0;
    uint32_t blockStart = 0;
    uint32_t blockEnd = 0;
    // Target coordinates of the region's first glyph, the point its lines
    // were drawn at.
    float x = 0.0f;
    float top = 0.0f;
};

// Fills the region's marker wash and then its search matches through the two
// shared passes, in the order both ports paint them: the mark under the
// search fill, the current match on top of the weaker ones.
void PaintRegionWashes(platform::RenderTarget& target,
                       platform::TextRangeSource& source,
                       const OverlayRegion& region, const PaintBand& band,
                       const std::vector<TextMatch>& markers,
                       const std::vector<TextMatch>& matches, int current,
                       const platform::Color& markerFill,
                       const platform::Color& markerLine,
                       const platform::Color& matchFill,
                       const platform::Color& currentMatch);

}  // namespace layout
