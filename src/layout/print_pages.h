// Printing, shared half: the paper, and the cut that turns one laid out
// document into pages.
//
// The geometry is seam units (1/96 inch) throughout, the unit the layout,
// the theme and both render targets speak. A platform converts the print
// panel's points once, at the boundary, and never again: a body line of 16
// seam units is 12 pt on paper, which is what the reading view at 100% means.
//
// The cut is a pure function of the laid out blocks, so it is testable
// without a printer, and the same answer drives both ports.

#ifndef MARKDOWNIT_SRC_LAYOUT_PRINT_PAGES_H_
#define MARKDOWNIT_SRC_LAYOUT_PRINT_PAGES_H_

#include "layout/displaylist.h"
#include "layout/document_layout.h"

#include <cstddef>
#include <vector>

namespace layout {

// The paper in seam units (1/96 inch). The platform converts the print
// panel's points once, at the boundary.
struct PrintPage {
    float width = 793.7f;    // A4: 595.28 pt x 96/72
    float height = 1122.5f;  // 841.89 pt x 96/72
    float margin = 75.6f;    // 56.7 pt x 96/72, a 2 cm margin
    float ContentWidth() const { return width - 2.0f * margin; }
    float ContentHeight() const { return height - 2.0f * margin; }
};

// One printed page in layout coordinates. top and bottom are cut lines in
// the layout's own y, so a band may begin and end inside a block.
struct PrintBand {
    float top = 0.0f;
    float bottom = 0.0f;
    size_t firstBlock = 0;  // index into DocumentLayout::blocks
    size_t lastBlock = 0;   // one past the last block the band touches
};

// Cuts the laid out document into bands of ContentHeight(). A block that
// fits a page but not the room left on this one starts the next page. A
// block taller than a page is cut, and continues above the next band.
std::vector<PrintBand> PaginateDocument(const DocumentLayout& laid,
                                        const PrintPage& page);

// The items of the list that intersect the band. Every PushClip and its
// PopClip survive whatever their rect, because the player pairs them and a
// dropped half clips the rest of the page.
display::DisplayList SliceForBand(const display::DisplayList& list,
                                  const PrintBand& band);

}  // namespace layout

#endif  // MARKDOWNIT_SRC_LAYOUT_PRINT_PAGES_H_
