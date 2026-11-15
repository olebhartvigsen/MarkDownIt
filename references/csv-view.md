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

Two things have to move for the band to stay: its own items and its words. The
walk emits the header row's fill, hairlines and funnels twice, once into the
page and once into `LaidTable::header`, both in document coordinates, so a port
pins the band by replaying that copy at its top edge with no re-measure. The
words come from the row's measured lines, which the painters already place per
row, at the pinned row's own top. The press areas move with the band: a table
whose header is pinned answers for its buttons at the viewport's edge, so a
reader who scrolls can still filter.

A pinned band covers the rows under it, so it is opaque: the header's fill is
the band's own, and nothing shows through.

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
- **The column's distinct values**, most frequent first, with a count when a
  value appears more than once, and a tick on the one the column is filtered
  to. A numeric column lists its values in numeric order, not text order.
- At most 40 values. A menu is not a table; a wider column is narrowed by
  picking the value that matters, not by scrolling a list of a thousand.

A value picked from the menu must equal the cell, case aside. A query typed
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
