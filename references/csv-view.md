# CSV tables: a data file in the document engine

A `.csv`, `.tsv` or `.tab` file opens as a **table document**. The file is
read by `src/csv.*`, bridged to the node list by `src/csvdocument.*`, and
drawn by the table layout and the table painters that markdown already uses.
Nothing about the page is new: the same window, the same title, the same
file watch, the same scroll, the same home screen card.

The file is a viewer. No markdown formatting, no table editing, no saving.
A spreadsheet is a spreadsheet program's job; reading one that arrived next
to a folder of markdown files is this app's job.

## Why the same engine

The table view was already there: `LayoutTableBlock` splits the columns,
measures the cells and places the rows, the display list carries the fills
and the grids, and each renderer paints the measured lines it is handed.
A CSV is a table with an opinion about its width, so the work is a parser
and a bridge, not a second renderer. Two renderers would drift, and the
drift would show as a column that lines up in markdown and not in CSV.

## The format

- **Quoting** follows RFC 4180: a quoted field may hold the delimiter, a
  line end and `""` for a literal quote. Quotes are only special inside a
  field, and an unquoted field keeps its bytes.
- **Line ends**: CRLF, LF and a lone CR all end a record. A line end inside
  quotes belongs to the field.
- **Byte order marks** are stripped: UTF-8 with and without, and a UTF-16 or
  UTF-32 mark is reported as unreadable rather than decoded as noise.
- **Delimiter sniffing** scores `,`, `;`, tab and `|` on the first twenty
  records: the delimiter whose field count agrees with itself most often
  wins, and a count of one field is no evidence. A `.tsv` or `.tab` file
  says tab outright, and the extension beats the sniff.
- **The header** is a guess: the first record is a header when every cell is
  filled, every cell is different, and no cell is a number. The guess is
  wrong on some files, and the switch that fixes it comes with the filters.
- **Numbers** are plain decimal only: an optional sign, digits, one dot and
  an optional exponent. `1.234,56`, `1,234` and `N/A` are text. A column is
  right aligned when at least four in five of its filled body cells are
  numbers, so one gap does not swing it and one stray word does.
- **Blank lines** before the first record are skipped; interior blank records
  are kept as empty rows, because a single column file may mean them.

## The columns size to their content, and the page scrolls sideways

A data table does not split its width equally the way a markdown table does.
Every column is as wide as the longest word a reader has to read whole in
it, header and body alike, plus the cell padding, and no wider than thirty
two characters of the fixed face. When the columns together fit the page,
the spare width is shared equally on top, so a small file still fills the
column the way it always has. When they do not fit, every column keeps its
width and the table becomes wider than the page column: the view scrolls
horizontally, and the scrollbar is the affordance.

The rule came out of a 33 column localisation export. Splitting 33 columns
equally left the prose columns one and a third DIP wide, which wraps a
paragraph one glyph per line: the table was unusable, and the file read as
one that could not be opened at all. A fixed grid of columns cannot show
prose; a scrolled grid can.

Two consequences are part of the rule:

- The row washes and the hairline grid span the table's own width, not the
  page column, so the columns a reader scrolls to are not past the end of
  their own stripes. `LaidBlock::width` carries that width, and the walk's
  own width is what both ports size the horizontal scroll range from.
- The filter funnels travel with the columns: their hit rectangles are in
  the same document coordinates as the caret, and the Windows shell adds the
  horizontal scroll to the click before asking (it used to add only the
  vertical one, which put every press a scroll's width off).

The demand is found by counting, not by measuring. Every cell of a data
table is drawn in the fixed face, where all glyphs advance the same, so the
word with the most code points in a column is also its widest. The layout
takes that one word, measures it once per column, and never asks the engine
for a layout per word: a 454 by 33 export holds twelve thousand words, and
twelve thousand text layouts to open a file is a hang, not a measurement.
Markdown tables keep the per word walk, because a proportional face does not
let a count stand in for a measure.

## Caps

| Cap | Value | What the page says |
|-----|-------|--------------------|
| Rows | 20,000 | "Showing the first 20,000 of N rows." |
| Columns | 64 | "The file has more than 64 columns; the rest are not shown." |
| Cell | 1 MiB | "Some cells were cut at 1 MiB." |

The notice is a paragraph under the table, so a reader is never shown a
partial table that pretends to be whole. The caps are constants in
`src/csv.h`; the visible-range painting that would lift the row cap is the
next slice of the plan.

### What the caps cost

Measured with the fake engine (`/opt/data/cache/scratch/probe_csv.cpp`,
`-O2`), which charges the layout's own work and not a platform text
stack's, so a real engine's open is higher:

| Rows x columns | Parse | Layout |
|----------------|-------|--------|
| 1,000 x 12 | 1.6 ms | 10 ms |
| 5,000 x 12 | 7.9 ms | 51 ms |
| 20,000 x 12 | 27 ms | 201 ms |
| 2,000 x 40 | 7.3 ms | 64 ms |
| 5,000 x 40 | 19 ms | 160 ms |

The layout is the cost: the table lays out every row up front, so a 20,000
row file pays a fifth of a second on every re-layout (zoom, resize). That
is the price of the row cap as it stands, and the reason a much bigger cap
waits for the visible-row painting.

## The header stays

A table can be longer than the window, and a column of figures with no names
over it is a column of figures. So the header row is pinned: once it has
scrolled above the viewport, while the table still covers the viewport's top
edge, the row is drawn again at that edge, above the body that scrolls under
it. The rule is one shared function, `layout::PinnedHeader`, which both ports
call before they paint a table.

The whole band travels: its surface, its hairlines, its funnels and its words.
The walk emits the header row twice, once into the page and once into
`LaidTable::header`, and the copy carries text items as well as shapes, so a
port pins the band by replaying that list at its top edge with no re-measure and
no second painter. A port that replays the shapes and then asks its table
painter to draw the row again at the edge draws nothing: the painter works from
the row's measured place, which is not where the band is.

The copy is in the walk's coordinates and `LaidTable::headerTop` is where its
own top sits in them, so the translate is `edge - headerTop`. Subtracting the
block's document y instead is one page padding too much, and the band lands just
above the viewport, invisible. On the mac the pass runs inside the page's own
draw, with the scroll offset the layer keeps for hit testing: the renderer draws
the page whole in document coordinates and the view is the only place that knows
which part of it is on screen. The press areas move with the band too: a table
whose header is pinned answers for its buttons at the viewport's edge, so a
reader who scrolls can still filter.

A pinned band covers the rows under it, so it is opaque, and it carries its own
surface even when the style paints the header row in the page's paper and
zebras the body instead: `layout::PinnedHeaderFill` gives the header's own fill
where a style has one and the muted row fill where it does not, so a pinned
header still reads as a header.

Every table pins, a markdown table in a document as much as a table file, and
the pin belongs to reading: in edit mode the caret and the hit test answer for
the row where it was measured, so a pinned band would put the words somewhere
the caret is not, and the pin waits for view mode.

Two things made the pin invisible when it was built: the mac replayed the band
with the wrong origin, so its fills and hairlines sat a pad left and a scroll
above the words that were pinned, and the band had no surface at all in a
style that leaves the header on the paper.

## The filter buttons

Every column's header carries a funnel at its right edge, the way a
spreadsheet does. A press on it opens that column's menu and never moves the
caret: the button owns its press.

The target is the header row's whole height and 28 points wide
(`tableFilterHit`), which is wider than the mark and wider than the gutter the
cell text keeps clear: seven points of ink is a hard thing to hit. The mark it
draws stays seven points (`tableFilterMark`), so the header reads quietly
while the target stays generous. `LaidTable::filterButtons` carries the
rectangles and `filterCenters` the centre of each mark, so the press area and
the six-point glyph cannot drift apart, and a painter that redraws the band
above the scroll draws the same mark in the same place.

The menu is the platform's own, built from one shared list
(`csvdoc::FilterMenu`), so the two ports cannot offer different rows:

- **Show all rows in this column** clears the column. It is ticked when the
  column has no filter, so the menu always shows which state it is in.
- **Filter by text** asks for a piece of text and keeps the rows whose cell
  contains it, case aside. It is how a reader asks for a part of a value
  (every row that mentions konfirmation) and how they reach a value the list
  below leaves out. A column filtered that way says so in its own row, and
  the line under the table names the text.
- **The values**, under a divider. The divider marks the first value; it does
  not stand in for it. It used to replace it on both ports, which dropped the
  most frequent value of every column from its own menu: the row read as
  blank, the values started one late, and the one value that carries most of
  the table was the one value a reader could not pick.
- **The column's distinct values**, most frequent first, with a count when a
  value appears more than once, and a tick on the one the column is filtered
  to. A numeric column lists its values in numeric order, not text order.
- At most 40 values. A menu is not a table; a wider column is narrowed by
  picking the value that matters, not by scrolling a list of a thousand.

A value picked from the menu must equal the cell, case aside, with the cell
trimmed the way the value list trims it. A cell written with a leading space
is the same value on screen and in the menu, so it has to be the same row
when it is picked: otherwise the menu counts two rows under one value and
keeps one. A query typed
by hand only has to be found in the cell (`csvfilter::Matches`), which is the
same distinction Excel draws between ticking a value and typing in its
search box. The typed path exists in the model and has no surface yet.

Filtering happens **at the bridge**, not in the renderer: the visible rows
become the table node, so the table layout, the display list and both
painters are untouched, and a filtered table is laid out for the rows it
shows. The funnel itself is a display list polygon, drawn by the same
`display::Play` as everything else, and the hit rects come from the shared
layout (`LaidTable::filterButtons`, `layout::FilterButtonAt`). A filtered
column's funnel takes the link color, so a table says which of its columns
is doing the work.

The line under the table counts what the filters left: "Showing 2 of 4
rows.", and when nothing matches it says so and says what to do about it.

## The header

A data table marks its header whatever prose style the reader chose: the
header row takes the alternate fill, the body is not zebraed, and every
header cell is drawn semibold with the fixed face. A prose table keeps its
style's own rules. The header is also the default reading of the first
record: it is a header unless it cannot be one (a single record, no text in
it at all, or nothing but numbers).

## The fixed font

A table document asks for the code face in every cell, header and body
alike, at the code font size. `Node::tableMonospace` is the flag, and
`BuildCellRuns` in `src/layout/table_layout.cpp` reads it. A markdown table
keeps the body face and takes the code face only for its code spans.

The fixed face is what makes a column of figures readable down one edge: the
digits sit at the same x whatever the value, so a wrong order of magnitude
is visible without reading the number.

## Read only, and what that greys

`FileFormat::isTable` marks the document, `DocumentController::InTableDocument`
answers it, and `commands::Context::tableDocument` carries it to the one
command state both ports read. In a table document:

- Editing commands, markdown formatting, table edits and the edit switch are
  off (the switch has no second mode to switch to).
- Find is off: a match has to be drawn where the text is, and a table paints
  its own cells. The column filters are the search a table has.
- Source view stays on: it shows the bytes the file holds, which is exactly
  what a reader checking a quoting bug wants.
- Zoom, the outline rule (a table has no headings), the title, the watch and
  the home screen card all behave as they do for any document.

A file opens in reading mode, and opening a table while the window was
editing another document leaves it in reading mode.

## Where the pieces live

| Piece | File |
|-------|------|
| The reader | `src/csv.h`, `src/csv.cpp` |
| The bridge to the node list | `src/csvdocument.h`, `src/csvdocument.cpp` |
| The kind switch both ports call | `src/documentparse.h`, `src/documentparse.cpp` |
| The fixed font in the layout | `Node::tableMonospace`, `BuildCellRuns` |
| The kind, the filters, the decode | `src/documentfile.*` |
| When a command may act | `src/commandstate.*` |

## Not built yet

- **A typed query in the menu.** The model has the contains match
  (`csvfilter::Matches`), the menu offers values only.
- **The visible-row painting** that makes a much higher row cap affordable,
  the header-row switch and the row number gutter (S5).
- **Saving** a table. It waits for a quoting model that can tell `"a,b"`
  from `a,b` when a cell is written back.
