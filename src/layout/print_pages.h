// Printing, shared half: the paper, and the cut that turns one laid out
// document into pages.
//
// The geometry is seam units (1/96 inch) throughout, the unit the layout,
// the theme and both render targets speak. A platform converts the print
// panel's points once, at the boundary, and never again: a seam unit is
// 1/96 inch on paper whatever the sheet's dpi. `PaperMetrics` sets the type
// below the reading view's sizes, because those are tuned for a screen.
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

// Where a band's content sits on its page, in seam units: the page's own
// margin, with the band's distance from the document's top taken back out.
// A slice keeps document coordinates, so a painter applies this placement and
// then plays the slice. That is the whole rule, in one place, for both ports.
struct PagePlacement {
    float x = 0.0f;
    float y = 0.0f;
};

PagePlacement PlacementForBand(const PrintPage& page, const PrintBand& band);

// The items one page draws: the band's slice of the page list, plus the words
// of every table the band touches. The display list carries a table's washes
// and hairlines but not its cell text, because the window paints that from the
// table's own record; a page replay has no window, so the record's copy comes
// with it. Clips are kept in pairs, as everywhere.
display::DisplayList PageItems(const DocumentLayout& laid,
                               const PrintBand& band);

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

// Paints one band as one page onto any seam target: the band's items,
// placed by PlacementForBand and drawn by the shared player. The DC
// driver, the DXGI preview and the command lists come here, so the
// placement rule has one executor across the float pipelines. The pdfio
// writer is the one medium outside: its stream coordinates are
// golden-backed %.6f bytes, and a float round-trip of the placement
// moves the sixth decimal, so that target adds PlacementForBand in
// double inside its own MapX/MapY and keeps its bytes as they are.
void PaintBandPage(platform::RenderTarget& target,
                   const DocumentLayout& laid, const PrintPage& page,
                   const std::vector<PrintBand>& bands, size_t index);

// The page from a print panel's description: the sheet's own size, and
// the imageable rectangle the printer can actually reach, both in seam
// units, measured from the sheet's top left. The margin keeps the
// unprintable strip outside the layout: the widest of the four edges
// around the imageable area decides it, and where the sheet has room the
// 2-cm rule stands, shrinking on a small sheet as the paper allows. A
// driver that reports no strip at all passes the sheet twice and gets
// the DC path's own margin rule, the same inset today's builds draw.
PrintPage PrintPageFromDescription(float sheetWidth, float sheetHeight,
                                   float imageableX, float imageableY,
                                   float imageableWidth,
                                   float imageableHeight);

}  // namespace layout

#endif  // MARKDOWNIT_SRC_LAYOUT_PRINT_PAGES_H_
