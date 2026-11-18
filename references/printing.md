# Printing

File ▸ Print (⌘P on the mac, Ctrl+P on Windows) prints the open document,
one page per sheet. The page is the reading view's rendering of the same
document at 100%, cut into sheets. It is not a screenshot, and it is not a
second renderer.

## The pieces

| Piece | Where | What it owns |
|---|---|---|
| The page and the cut | `src/layout/print_pages.*` | the paper, `PrintBand`, `PaginateDocument`, `SliceForBand` |
| Bounds of a drawing | `display::ItemBounds` (`src/layout/display_player.cpp`) | the rectangle one display list item occupies, for the cut |
| The layout of a page | `Renderer::PrintLayout` (Windows), `MacRenderer::PrintLayout` (macOS) | the shared walk at 100% in reading mode, outside the frame's cache |
| The painter | `display::Play` | the same player the window uses, on a different target |
| The Windows driver | `src/print_win.*` | the panel, the printer DC, one page per band |
| The mac driver | `src/macos/print_mac.*` | the print panel over a view, and the PDF writer |

Nothing above the drivers knows what a printer is. Both ports hand the same
`DocumentLayout` and the same `PrintBand` list to the same player.

## The page

Geometry is seam units (1/96 inch) everywhere, the unit the layout, the theme
and both render targets already speak. The platform converts the panel's
points once, at the boundary, and never again. A body line of 16 seam units
is 12 pt on paper, which is what the reading view at 100% means.

`PrintPage` defaults to A4: 793.7 × 1122.5 seam units, a 2 cm margin (75.6).
The margin shrinks on a small sheet instead of eating the page. Windows
builds the page from the printer's own printable area, so the unprintable
edge of the sheet stays outside the layout. The mac builds it from the
panel's imageable area. The QA walk uses the defaults on both ports, which is
what makes its numbers comparable.

## The cut

`PaginateDocument` walks the laid out blocks and closes a band at
`ContentHeight()`:

- A block that fits the room left stays on the page.
- A block that starts past the page's bottom closes the page before it. The
  gap in between stays on the closed page, so no content is dropped.
- A block that would fit an empty page but not the room left moves whole.
  This is the widow rule: a paragraph is never cut for want of a few units.
- A block taller than a page is cut at the page's bottom and continues on
  the next. A sixteen page table prints as sixteen pages, and the cut can
  land through a row. This is the known cost of the simple rule.
- Pagination always terminates: a band that closes without moving the cut
  forces one page of progress.

`SliceForBand` gives one page its items by their bounds. Every `PushClip` and
`PopClip` survives whatever its rect, because the player pairs them and a
dropped half clips the rest of the page. An item exactly on a cut belongs to
the page its own y is on, and to exactly one page, so two sheets cannot both
draw a table's rule.

## What is on the page, and what is not

On the page: headings, paragraphs, lists, quotes, code cards, tables, rules,
every spacing the theme owns. The layout is the reading view's, so the page
matches what the window shows at 100%. The light palette is used whatever the
window is wearing: paper is white.

Not on the page, in this version:

- Mermaid diagrams print as an empty band. The painter draws them from
  platform resources, and there is no print path into it yet.
- An SVG fence prints as an empty band for the same reason.
- An inline image is not resolved onto the page. The player skips an image
  with no handle, so the band where it sits is blank.
- There are no page numbers and no running head.
- A block taller than a page is cut through its line.
- A page range in the panel prints every page. Printing is a whole document
  operation, and the panel's page controls are not read.

Each of these is a picture that is missing something, never a wrong picture:
no gaps appear where content should be, and no content is printed twice.

## The QA walk

`MARKDOWNIT_QA_STATES=print-pages` lays the open document out for the default
A4 page and reports what the cut did:

```
qa print-pages: pages=3 items1=57 items2=54 top2=971.3 width=642.5 height=971.3 wrote=1
```

Both ports report the same numbers for the same file, because the page, the
margin and the cut are shared. On the mac, `MARKDOWNIT_QA_PDF` names a file
and the same pages are written to it as a PDF, so the pagination can be
looked at without a printer. The launch smoke asserts that the showcase
fixture makes more than one page and that the PDF carries them.

## The panel

A heading is never the last block on a page: its section would start on the
next sheet, so the cut moves up and the title travels with the block it
introduces. A heading that is a page's first block stays, or the page would be
empty.

A table is the one block whose words are not in the page list: the window
paints its cells from the table's own record, because selection and find marks
sit between those lines. `layout::PageItems` therefore appends the record's
copy (`LaidTable::cellsText`) for every table the band touches, so a page
replay draws the table instead of a grid of empty boxes.

A slice keeps document coordinates, and `layout::PlacementForBand` is the one
rule for turning them into a page: the page's margin, with the band's own
distance from the document's top taken back out. Both painters apply it, so a
page the two ports print is the same page.

macOS lets the operation own its panel, shown as a sheet on the window: choosing
a printer, choosing Save as PDF and cancelling are AppKit's business, and the
pages go wherever the reader points them. The run needs a delegate and a
selector to report to; asked to run with neither, AppKit put nothing on the
screen at all.

Windows opens the classic print panel (`PrintDlg`) and prints into an
`ID2D1DCRenderTarget` bound to the printer's DC. The target carries the printer's dpi, so one seam unit is
one DIP on paper and the drawing code has no scale factor of its own. Text is
grayscale antialiased: subpixel text relies on the display's stripes and
prints as coloured fringes.

The mac opens an `NSPrintOperation` over a view that answers one page per
band, so AppKit owns the panel, the preview and the progress. The same page
painter writes the PDF, through `CGPDFContext`. A PDF context is bottom-up
and the layout is top-down, so the flip happens once, in the driver.

Both drivers treat a cancelled panel as nothing at all: no error, no message,
no change to the document.

## Tests

- `tests/print_pages_test.cpp` holds the cut: an empty document, a short one,
  a page that fills exactly, a widow, a block taller than the page, thirty
  real paragraphs through the shared walk, the clip pairs, and the item on
  the cut line.
- `tests/display_player_test.cpp` asserts the bounds of every alternative of
  the display list item variant. `ItemBounds` has one overload per
  alternative and no generic fallback, so a new item type stops compiling
  until its bounds exist. That is the guard against a page silently losing a
  kind of drawing.
