// The replay of a finished display list onto any platform
// RenderTarget, plan Batch 3. The renderers translate once for scroll
// and hand the whole list here; the player owns the two emulations
// the seam has no op for, the dash flattening and the run
// decorations, so both painters stroke the same pen.

#ifndef MARKDOWNIT_SRC_LAYOUT_DISPLAY_PLAYER_H_
#define MARKDOWNIT_SRC_LAYOUT_DISPLAY_PLAYER_H_

#include "layout/displaylist.h"

#include <functional>

namespace display {

// Answers the platform handle an Image item draws with; a renderer
// with a decode cache passes its lookup here.
using ImageResolver = std::function<platform::BitmapHandle(const Image&)>;

// Plays the list with no image resolver: the Image items are skipped,
// which is what a renderer without a cache shows.
void Play(const DisplayList& list, platform::RenderTarget* target);

// Plays the list against an image resolver: every Image item is
// looked up and handed to the seam as a bitmap. A null resolver skips
// the items, and a resolver that answers a null handle skips that
// one image, because nothing is decoded to draw yet.
void Play(const DisplayList& list, platform::RenderTarget* target,
          const ImageResolver& imageResolver);

// Plays the list with every item's geometry shifted by (dx, dy) before
// it reaches the target. This is the print page placement living in the
// player: the DC driver, the DXGI preview, the command lists and the pdfio
// writer all draw moved items instead of each target shifting its own way,
// so the one rule (layout::PlacementForBand) has one executor. The shift
// is exact addition; nothing is scaled, clipped or resolved differently,
// and a (0, 0) origin is the plain Play above.
void PlayWithOrigin(const DisplayList& list, platform::RenderTarget* target,
                    float dx, float dy);

}  // namespace display

#endif  // MARKDOWNIT_SRC_LAYOUT_DISPLAY_PLAYER_H_
