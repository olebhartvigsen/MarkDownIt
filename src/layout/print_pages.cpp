// The cut that turns one laid out document into pages, and the slice that
// gives one page its own items. Plan: .hermes/plans/2026-11-15_204703-printing.md
//
// The rules, in the order the walk applies them:
//
// - A block that fits the room left on the page stays there.
// - A block that starts past the page's bottom closes the page before it, and
//   the gap in between stays on the closed page: no content is dropped.
// - A block that would fit an empty page but not the room left moves whole to
//   the next page, which is the widow rule. A paragraph is never cut in half
//   for want of a few units.
// - A block taller than a page is cut at the page's bottom and continues on
//   the next. This is the known cost of the simple rule (printing.md records
//   it): a very tall table or code block can be cut through a line.
// - Pagination always terminates: a band that closes without moving the cut
//   forces one page of progress.

#include "layout/print_pages.h"

#include "layout/display_player.h"

#include <algorithm>
#include <limits>
#include <variant>

namespace layout {

namespace {

// The tallest bottom any block reaches, so the last page covers the content
// even when the last block was moved whole.
float ContentBottom(const std::vector<LaidBlock>& blocks) {
    float bottom = 0.0f;
    for (const LaidBlock& b : blocks) {
        bottom = (std::max)(bottom, b.y + b.height);
    }
    return bottom;
}

// A heading is never the last thing on a page: its section starts on the next
// sheet, so the reader meets a title with nothing under it. The cut moves up
// to the heading and the two travel together. A heading that is the page's
// first block stays where it is, or the page would be empty and the walk would
// not move; a trailing heading with nothing after it also stays.
float KeepHeadingWithNext(const std::vector<LaidBlock>& blocks, float top,
                          float bottom) {
    bool hasNext = false;
    for (const LaidBlock& b : blocks) {
        if (b.y >= bottom) {
            hasNext = true;
            break;
        }
    }
    if (!hasNext) return bottom;
    const LaidBlock* last = nullptr;
    for (const LaidBlock& b : blocks) {
        if (b.y >= bottom) break;
        last = &b;
    }
    if (!last || last->kind != BlockKind::Heading) return bottom;
    if (last->y <= top + 0.001f) return bottom;
    return last->y;
}

void FillBlockRange(const std::vector<LaidBlock>& blocks, PrintBand* band) {
    band->firstBlock = blocks.size();
    band->lastBlock = blocks.size();
    for (size_t i = 0; i < blocks.size(); ++i) {
        if (blocks[i].y + blocks[i].height <= band->top) continue;
        if (band->firstBlock == blocks.size()) band->firstBlock = i;
        if (blocks[i].y >= band->bottom) {
            band->lastBlock = i;
            return;
        }
        band->lastBlock = i + 1;
    }
}

}  // namespace

std::vector<PrintBand> PaginateDocument(const DocumentLayout& laid,
                                        const PrintPage& page) {
    std::vector<PrintBand> bands;
    const std::vector<LaidBlock>& blocks = laid.blocks;
    const float pageHeight = page.ContentHeight();

    // Paper with no room: one band over everything, so a caller still
    // prints something instead of dividing by zero.
    if (!(pageHeight > 0.0f)) {
        PrintBand only;
        only.bottom = ContentBottom(blocks);
        FillBlockRange(blocks, &only);
        bands.push_back(only);
        return bands;
    }

    if (blocks.empty()) {
        PrintBand only;
        only.bottom = pageHeight;
        bands.push_back(only);
        return bands;
    }

    float top = 0.0f;
    size_t i = 0;
    // Every pass either consumes a block or moves the cut down, and the
    // guard is the backstop for a number that is not finite.
    const size_t guard = blocks.size() * 4 + 16;
    for (size_t step = 0; step < guard; ++step) {
        const float pageBottom = top + pageHeight;
        if (i >= blocks.size()) break;
        const LaidBlock& b = blocks[i];
        const float blockBottom = b.y + b.height;

        if (b.y >= pageBottom) {
            // The block starts on a later page. The gap before it belongs to
            // the page that is closing.
            PrintBand band;
            band.top = top;
            band.bottom = KeepHeadingWithNext(
                blocks, top, (std::max)(pageBottom, b.y));
            FillBlockRange(blocks, &band);
            bands.push_back(band);
            top = band.bottom;
            continue;
        }

        if (blockBottom <= pageBottom) {
            ++i;
            continue;
        }

        if (b.height <= pageHeight && b.y > top) {
            // The widow rule: the whole block moves, the page closes where
            // the block would have begun.
            PrintBand band;
            band.top = top;
            band.bottom = KeepHeadingWithNext(blocks, top, b.y);
            FillBlockRange(blocks, &band);
            bands.push_back(band);
            top = band.bottom;
            continue;
        }

        // Taller than a page, or it starts exactly at the cut: cut it here.
        PrintBand band;
        band.top = top;
        band.bottom = pageBottom;
        FillBlockRange(blocks, &band);
        bands.push_back(band);
        top = pageBottom;
    }

    // The page the content ends on. Content that ends exactly on a cut gets
    // no trailing blank sheet.
    const float contentBottom = ContentBottom(blocks);
    if (contentBottom > top) {
        PrintBand band;
        band.top = top;
        band.bottom = (std::max)(top + pageHeight, contentBottom);
        FillBlockRange(blocks, &band);
        bands.push_back(band);
    } else if (bands.empty()) {
        PrintBand band;
        band.top = 0.0f;
        band.bottom = pageHeight;
        FillBlockRange(blocks, &band);
        bands.push_back(band);
    }

    return bands;
}

display::DisplayList PageItems(const DocumentLayout& laid,
                               const PrintBand& band) {
    display::DisplayList out = SliceForBand(laid.list, band);
    for (const LaidBlock& block : laid.blocks) {
        if (!block.table) continue;
        // A table that touches the band brings its words; the paper clip cuts
        // them where the page ends, the same way it cuts a paragraph's line.
        if (block.y >= band.bottom || block.y + block.height <= band.top) {
            continue;
        }
        if (block.table->cellsText.items.empty()) continue;
        out.items.insert(out.items.end(), block.table->cellsText.items.begin(),
                         block.table->cellsText.items.end());
    }
    return out;
}

PagePlacement PlacementForBand(const PrintPage& page, const PrintBand& band) {
    PagePlacement place;
    place.x = page.margin;
    place.y = page.margin - band.top;
    return place;
}

display::DisplayList SliceForBand(const display::DisplayList& list,
                                  const PrintBand& band) {
    display::DisplayList out;
    out.width = list.width;
    out.height = band.bottom - band.top;
    out.items.reserve(list.items.size());
    for (const display::Item& item : list.items) {
        // Clips are kept in pairs, always: the player pushes and pops in
        // order, and a dropped push with a surviving pop clips the rest of
        // the page away.
        if (std::holds_alternative<display::PushClip>(item) ||
            std::holds_alternative<display::PopClip>(item)) {
            out.items.push_back(item);
            continue;
        }
        const platform::Rect b = display::ItemBounds(item);
        bool inside = false;
        if (b.height <= 0.0f) {
            // A rule is a hairline: it belongs to the page its own y is on,
            // and to exactly one, so two pages cannot both draw it.
            inside = b.y >= band.top && b.y < band.bottom;
        } else {
            inside = b.y < band.bottom && b.y + b.height > band.top;
        }
        if (inside) out.items.push_back(item);
    }
    return out;
}

void PaintBandPage(platform::RenderTarget& target,
                   const DocumentLayout& laid, const PrintPage& page,
                   const std::vector<PrintBand>& bands, size_t index) {
    const PagePlacement place = PlacementForBand(page, bands[index]);
    display::PlayWithOrigin(PageItems(laid, bands[index]), &target,
                           place.x, place.y);
}

PrintPage PrintPageFromDescription(float sheetWidth, float sheetHeight,
                                   float imageableX, float imageableY,
                                   float imageableWidth,
                                   float imageableHeight) {
    PrintPage page;
    page.width = sheetWidth;
    page.height = sheetHeight;
    // The strip around the imageable area, each side of it. The widest
    // side is the least margin that keeps the layout inside the rectangle
    // the printer can reach, whatever the tray or the driver did.
    const float left = imageableX;
    const float top = imageableY;
    const float right = sheetWidth - imageableX - imageableWidth;
    const float bottom = sheetHeight - imageableY - imageableHeight;
    float edge = (std::max)((std::max)(left, top),
                           (std::max)(right, bottom));
    edge = (std::max)(edge, 0.0f);
    // The 2-cm inset the DC path draws, shrinking on a small sheet. A
    // sheet with no reported strip keeps exactly that rule, and a sheet
    // whose unprintable edge is wider takes the edge instead.
    const float smallest = (std::min)(sheetWidth, sheetHeight);
    const float inset = (std::min)(75.6f, (std::max)(0.0f, smallest * 0.15f));
    page.margin = (std::max)(edge, inset);
    return page;
}

}  // namespace layout
