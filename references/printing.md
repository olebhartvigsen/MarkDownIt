# Printing

File ▸ Print (⌘P on the mac, Ctrl+P on Windows) prints the open document,
one page per sheet. The page is the same shared layout as the reading view,
cut into sheets, with the type set smaller for paper. It is not a screenshot,
and it is not a second renderer.

## The pieces

| Piece | Where | What it owns |
|---|---|---|
| The page and the cut | `src/layout/print_pages.*` | the paper, `PrintBand`, `PaginateDocument`, `SliceForBand` |
| The page from a panel's description | `layout::PrintPageFromDescription` (`src/layout/print_pages.*`) | the sheet, the imageable rectangle, the margin that keeps the unprintable strip outside the layout |
| The band painter | `layout::PaintBandPage` (`src/layout/print_pages.*`), `display::PlayWithOrigin` (`src/layout/display_player.*`) | the band's placement and the page's items, played through the shared player onto any target |
| Bounds of a drawing | `display::ItemBounds` (`src/layout/display_player.cpp`) | the rectangle one display list item occupies, for the cut |
| The layout of a page | `Renderer::PrintLayout` (Windows), `MacRenderer::PrintLayout` (macOS) | the shared walk with `PaperMetrics`, outside the frame's cache |
| The Windows driver | `src/print_win.*` | the classic panel and the printer DC, one page per band, the fallback of the modern path |
| The modern print contract | `src/print_winrt.*`, `src/print_contract_abi.h` | the Windows.Graphics.Printing contract: the dialog's preview pane fed page by page, and the final job as command lists |
| The print-path PDF | `src/print_pdf.*` | the bands written as a PDF: shared writer, exercised by the QA walk and its test |
| The mac driver | `src/macos/print_mac.*` | the print panel over a view |

Nothing above the drivers knows what a printer is. Both ports hand the same
`DocumentLayout` and the same `PrintBand` list to the same painter:
`layout::PaintBandPage` places the band by `PlacementForBand` and plays the
page's items, and the mix of media that come to it (the printer DC's target,
the preview surface, the print command lists) all draw the same page. The
one medium outside it is the pdfio writer, whose stream coordinates are
golden-backed at six decimal places; a float round-trip of the placement
would move them, so that target adds the same placement rule in double
inside its own `MapX`/`MapY`.

## The page

Geometry is seam units (1/96 inch) everywhere, the unit the layout, the theme
and both render targets already speak. The platform converts the panel's
points once, at the boundary, and never again: a seam unit is 1/96 inch on
paper whatever the printer's dpi. `PaperMetrics` shrinks the reading view's
body and spacing to five sixths, because a screen's 12 pt body reads as 12.8
pt on an A4 sheet and fills it too fast. Headings get a second rule: after the
scale, H1 through H6 are capped at 1.8, 1.45, 1.2, 1.1, 1.0 and 1.0 times the
paper body. Without the cap the Apple style still printed a 25 pt title over
a 10.6 pt body. The light palette is used whatever the window wears.

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
every spacing the theme owns. The layout is the reading view's, so line
breaks and block order are the same; only the type and its spacing are scaled
down for paper. The light palette is used whatever the window is wearing:
paper is white.

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
margin and the cut are shared. `MARKDOWNIT_QA_PDF` names a file on either
port and the same pages are written to it as a PDF: the mac through
`CGPDFContext`, Windows through the shared pdfio writer. The mac launch
smoke and the Windows capture smoke both assert that the showcase fixture
makes more than one page and that the file carries them.

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

Windows takes the modern route first: Ctrl+P opens the system print dialog
through the Windows.Graphics.Printing contract (`src/print_winrt.cpp`), with
the dialog's own preview pane fed page by page. The contract's calls arrive
on the print pipeline's thread, and no layout happens there: every build
request is messaged to the window, answered inside its own message loop by
the same walk the classic panel uses, and the finished page comes back as
read-only shared state. The preview is one D3D11 texture per page, drawn
through the shared band painter and handed to the dialog as a DXGI surface
at 96 dpi, so a seam unit is one pixel of the preview. The final job sends
the same pages as Direct2D command lists through `ID2D1PrintControl`, one
`AddPage` per band, and the preview and the paper agree because both are
the same bands. The paper follows the dialog: every change of printer,
paper or orientation triggers a new Paginate, the sheet is rebuilt through
`PrintPageFromDescription`, and the preview shows the chosen paper instead
of a default one. The status of the job reaches the app through the
task's Completed event: a failed job is said out loud, and a cancelled one
is silence. Setting `MARKDOWNIT_NO_MODERN_PRINT=1` asks for the second
path, which is also the automatic fallback whenever the contract cannot be
reached.

The second path is the classic one (`PrintDlg`, `src/print_win.cpp`), the
flow Windows itself ships in its own apps: the classic panel, the job
printed into an `ID2D1DCRenderTarget` bound to the printer's DC. The target
itself runs at the documented default of 96, where one DIP is one device
pixel, and the printer's own resolution is a scale in the transform: a
seam unit becomes `LOGPIXELSX / 96` device pixels, which is 1/96 inch on
paper. The DPI is a transform and not a property of the target because
some drivers size the target's internal bitmap by the property and hand
back pages a multiple too large, fonts first. Text is
grayscale antialiased: subpixel text relies on the display's stripes and
prints as coloured fringes. A job that cannot start, draw or finish is said
out loud, because a print that dies quietly reads as "this app cannot
print".

An interim standalone preview window was built and taken out again with
the 2026-11-19 decision: the standard flow is what the reader expects, and
with the contract the preview lives where it belongs, inside the dialog
the system owns.

The print-path writer (`src/print_pdf.cpp`) still writes the bands as a
PDF, page for page, in the page's own paper layout: base 14 fonts, no
embedding, one page per band. Glyphs the fonts cannot show are dropped and
counted (the WinAnsi limit), and the writer says how many, instead of a
reader wondering where a letter went. The QA walk and
`tests/print_pdf_test.cpp` hold it on both runners. A reader reaches a PDF
of the pages on Windows through the panel's Microsoft Print to PDF
printer, and the ribbon's Export as PDF (the office re-render of the
markdown) stays as it was. The mac keeps the panel's Save as PDF, which
AppKit renders.

One difference is ledgered: the previews live in different places, the mac
inside AppKit's own panel, Windows inside the system print dialog through
the contract, and both feed their pane from the same shared bands. The
preview follows the paper the dialog chose, so the pages the reader sees
are the pages that print. The classic fallback prints straight from its
panel with no preview, and there the old A4 note still applies: the page
the QA walk reports is the default A4, the sheet the printer takes is its
own paper.

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
- `tests/print_paint_test.cpp` holds the painter and the description: the
  player's origin shift (a zero origin is the plain play, a real shift moves
  every geometry exactly), the band painter's placement (its calls match
  playing the band's items through `PlacementForBand` by hand, band after
  band), and the page a description builds (the A4 rule, a small sheet, the
  widest edge, and the content that stays inside the imageable area).
- `tests/display_player_test.cpp` asserts the bounds of every alternative of
  the display list item variant. `ItemBounds` has one overload per
  alternative and no generic fallback, so a new item type stops compiling
  until its bounds exist. That is the guard against a page silently losing a
  kind of drawing.
- `tests/print_pdf_test.cpp` holds the writer: one page per band read back
  through pdfio itself, stable bytes once the file's own stamps are taken
  out, Danish inside the fonts' WinAnsi range, an emoji outside it counted
  as dropped, the table words the page list carries, and a white sheet
  whatever theme the window is wearing.
