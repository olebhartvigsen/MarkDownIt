#ifndef MARKDOWNIT_SRC_LAYOUT_FRAME_H_
#define MARKDOWNIT_SRC_LAYOUT_FRAME_H_

// The frame both ports paint: one replay of the display list under the
// column origin, then the overlay layer above the surface it drew. The
// owner's composition decision makes this order canonical (plan 2.2,
// decision 1): the card, the table and the words of the page are items,
// and the washes, the chips, the selection and the caret overlays run
// above them. A shell brings its own column target and its own overlay
// body; the order is not its to choose.

#include <functional>

#include "displaylist.h"
#include "display_player.h"
#include "platform/seams.h"

namespace layout {

// Replays `list` onto `columnTarget`, which the caller has already moved
// to the column's origin and scrolled, then runs `overlays`. `images`
// answers the pictures the list places.
void PaintFrame(platform::RenderTarget& columnTarget,
                const display::DisplayList& list,
                const display::ImageResolver& images,
                const std::function<void()>& overlays);

}  // namespace layout

#endif  // MARKDOWNIT_SRC_LAYOUT_FRAME_H_
