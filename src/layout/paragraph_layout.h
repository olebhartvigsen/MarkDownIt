// The shared paragraph layout of plan Batch 3: the body text block,
// plus the image charge every text bearing block reuses. The
// paragraphs, the lists and the quotes all reserve their images above
// their text with the same rule, so the rule lives here once.

#ifndef MARKDOWNIT_SRC_LAYOUT_PARAGRAPH_LAYOUT_H_
#define MARKDOWNIT_SRC_LAYOUT_PARAGRAPH_LAYOUT_H_

#include "layout/document_layout.h"

namespace layout {

// The height an image takes while its size is not known yet, the
// placeholder both renderers reserve for a remote picture until the
// decode answers.
constexpr float kImageFallbackHeight = 100.0f;

// The height of a blank line while reading: the caret paragraph next
// to a list keeps its line while editing, but reading collapses it to
// a thin strip, because the blank lines of the source separate blocks
// and should not open empty rows in the text.
constexpr float kReadBlankLineStripDip = 4.0f;

// Reserves the images of a text block above its text: each image is
// charged at the browser rule (its intrinsic size, shrunk to the
// column when it is wider, scaled by the page zoom) plus one paragraph
// gap, an unknown size charges the placeholder height. Fills the
// block's image urls and imagesHeight, emits the display::Image items
// stacked from the block's top, and returns the charged height.
float ChargeForImages(const LayoutCtx& ctx, const Node& node,
                      LaidBlock* block, float top);

}  // namespace layout

#endif  // MARKDOWNIT_SRC_LAYOUT_PARAGRAPH_LAYOUT_H_
