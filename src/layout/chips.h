#ifndef MARKDOWNIT_SRC_LAYOUT_CHIPS_H_
#define MARKDOWNIT_SRC_LAYOUT_CHIPS_H_

// The one inline code pill pass (plan 2.2, slice 10, owner decision 3).
// The display list owns the surface and this layer runs above it, under
// the words it covers: one rounded fill per fragment of a code span, the
// first fragment carrying the left cap and the last the right, square
// between them, so a span the engine wrapped reads as one pill.

#include "displaylist.h"
#include "platform/seams.h"
#include "theme.h"

namespace layout {

// The corner radius of a pill at zoom 1.
constexpr float kInlineCodeRadius = 6.0f;

// Paints every inline code pill of the Text items in `list`, in item
// order, with `originX`/`originY` as the list's own origin in the
// target's coordinates. Only items whose top lies inside
// [bandTop, bandBottom] are painted, both in list coordinates; the
// document pass passes the visible band, a table passes its row.
//
// The runs of one span are consecutive in the list and carry the same
// display::SourceRange, which is what lets the pass round only the two
// real ends. The list's own PushClip and PopClip are honoured, so a
// pill never leaves its table cell.
void PaintInlineCodeChips(platform::RenderTarget& target,
                          const display::DisplayList& list, float originX,
                          float originY, const LayoutMetrics& metrics,
                          const platform::Color& color, float zoom,
                          float bandTop, float bandBottom);

}  // namespace layout

#endif  // MARKDOWNIT_SRC_LAYOUT_CHIPS_H_
