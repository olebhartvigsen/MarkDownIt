// The frame: the replay order, one body.

#include "layout/frame.h"

namespace layout {

void PaintFrame(platform::RenderTarget& columnTarget,
                const display::DisplayList& list,
                const display::ImageResolver& images,
                const std::function<void()>& overlays) {
    display::Play(list, &columnTarget, images);
    // The overlays sit above the surface the replay just drew. A shell
    // that skipped this call, or ran it first, would paint its washes and
    // words under the card and the table grid.
    overlays();
}

}  // namespace layout
