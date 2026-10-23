# The macOS port: what is there and what is not

The plan is `.hermes/plans/2026-09-12_203000-macos-port.md`. This is the
state of the port against that plan, written on October 22, 2026, when
the branch `feat/macos-port` was green on both platforms. It was
refreshed on October 23, 2026, after the five-batch spec-gap closure.

## The gates

| Gate | Where it runs | State |
|------|----------------|-------|
| `Tests (macOS, Apple Silicon)` | GitHub Actions, `macos-26` | 7683 checks, 0 failures, 662 passed, 1 known gap |
| `Build (macOS, Apple Silicon)` | GitHub Actions, `macos-26` | the app bundle builds, is signed ad hoc and passes the bundle check |
| `Build (x64)` | GitHub Actions, `windows-2022` | green |
| The office and the mermaid oracles | GitHub Actions | green |

The macOS test job runs the same suite as the Windows job, on the real
runner, so the shared core is proven on both platforms. The bundle is
uploaded as an artifact.

The runner label is read from the `MACOS_RUNNER` repository variable
and falls back to `macos-15-arm64`. The variable is set to `macos-26`
because the `macos-15-arm64` queue sat for an hour and a half while
`macos-26` started at once. See `references/macos-build-pipeline.md`.

## The shape of the port

The shared code stays shared. What changed in it is small and mechanical:

- `src/parser.cpp` keeps the Windows structured exception handling
  behind `_WIN32`, and the crash trace, the settings and the theme are
  portable.
- `src/textlayout.h` is the seam that measures and places text. It has
  three implementations: DirectWrite on Windows, Core Text on macOS,
  and a plain one for the portable build. Every position in the seam
  is a count of UTF-16 code units.
- `src/layoutcache.h` and `src/layoutcache.cpp` use the seam and
  nothing else.
- `src/findbar_model.h` and `src/findbar_model.cpp` hold the search
  state and the navigation. The Windows find bar is a widget on top of
  the model, and the mac find bar is an AppKit view on top of the same
  model.
- `src/settings.h` is portable. The Windows registry code is behind
  `_WIN32`, and the mac build keeps the same struct as JSON.
- `src/tableedit.cpp` and `src/tableedit.h` hold the table commands,
  so the Windows build and the mac build change a table the same way.
- The removal of the inline formatting markers is in the shared
  formatting module, so the two builds clear the same markers.

Everything else lives in `src/macos/`:

| File | What it does |
|------|--------------|
| `seams.h` | The narrow interfaces the mac layer is written against |
| `text_engine_mac.*` | Core Text measurement and drawing |
| `renderer_mac.*` | The renderer: blocks, inline styles, tables, images, mermaid, SVG |
| `mermaid_painter_mac.*` | The mermaid shapes |
| `svg_mac.*` | The SVG reader and its painter |
| `welcome_mac.*` | The welcome screen with the recent files |
| `findbar_mac.*` | The find bar |
| `content_view_mac.*` | The view: the mouse, the keyboard, the caret, the selection |
| `app_mac.*` | The document, the editing, the lifecycle |
| `clipboard_mac.*` | The pasteboard, with the custom markdown format |
| `settings_mac.*` | The settings file |
| `menu_mac.*` | The menu bar |
| `outline_pane_mac.*` | The outline pane: the headings and the jump |
| `outline_nsview_mac.*` | The AppKit view that draws the outline pane |
| `cg_target_mac.h` | The Core Graphics render target, for the two views |
| `crash_trace_mac.*` | The crash breadcrumbs |
| `main_mac.mm` | The application, the window, the file opening |

## What the app can do

Everything the plan asked for in tasks 1 to 7 is there:

- The document opens, parses, lays out and draws, and it scrolls.
- The headings, the paragraphs, the lists, the tables, the quotes, the
  code blocks, the thematic breaks and the inline styles are drawn.
- The images load from a file and from a link, and they are drawn.
- The mermaid diagrams have a painter, and the SVG documents are read
  and drawn.
- The caret blinks, the selection is drawn, the mouse selects, and a
  double click takes a word and a triple click takes a paragraph.
- The editing, the undo, the autoformat, the formatting commands and
  the find bar are the shared ones. The find bar steps with Enter and
  Shift+Enter, closes with Escape, counts its matches and reports its
  replacements. The bold and italic commands follow the shared marker
  rules, including the typing format a toggle sets at an empty caret.
- The clipboard keeps the custom markdown format next to the text.
- The source view, the zoom, the wrapping and the width of the column
  are in the View menu.
- The menus carry the file, the edit, the view, the format and the
  office commands.
- The office commands import a Word file and export Word or PDF. Both
  exports show the compatibility report, the PDF export reports its
  page count, and an import starts an untitled, dirty document, so a
  Save cannot write the imported text over the file that was open
  before.
- The mermaid flowcharts are measured with the Core Text engine in the
  live renderer and cached by fence source and zoom, the way the
  Windows build measures with DirectWrite.
- The welcome screen shows the recent files as cards, and a click on a
  card opens that file.
- The commands that throw the document away ask about the changes
  first.
- The outline pane is there. It shows the headings, a collapsed heading
  hides its subtree, and a click on a heading moves the caret there.
  The pane scrolls on its own, the active heading follows the caret,
  the arrows, Enter, Home, End and the left and right arrows drive a
  selection, and the outline is a list of headings to assistive
  technology.
- The window keeps its place between the runs, and the settings live
  in `~/Library/Application Support/MarkDownIt/settings.json`.
- The bundle carries the icon and the document types, the CI signs
  it ad hoc, checks it and uploads it.
- The table commands are there: insert a table, add and remove a row
  or a column, align a column and split a cell. They are shared with
  the Windows build, so the two do the same thing.
- The highlights are there. The marked text is kept in a sidecar file
  next to the document, the same way the Windows build keeps it, and
  the marker is drawn behind the text with the theme colours.
- The clear formatting command is there. The inline markers are taken
  away by the shared formatting module, so the two builds clear the
  same markers.
- A file dropped on the window opens.
- The application menu has the command that asks the system to make
  MarkDownIt the reader of the markdown files.

## What is not there yet

There are no gaps left in the list the plan asked for. The outline pane
is the last one, and it is there: the View menu shows and hides it, it
sits on the left of the document in a split view, the reader can drag
the divider, and a click on a heading puts the caret there and scrolls
it into view.

What is left is the work that needs a person in front of a Mac:

- the first attempt at running the app on a Mac, on October 22, 2026,
  did not open the app for the person who tried it. The log from that
  machine names the two build defects that are fixed now (the bundle
  had no seal, and its minimum system version was empty); see
  "Signing and the first launch" in `references/macos-build-pipeline.md`
- the next attempt can be watched from the start-up breadcrumbs in
  `~/Library/Logs/MarkDownIt/crash_trace.log`
- the editing, the dragging, the printing and the file associations
  still wait for a hand run, and so does the layout: CI pins the
  layout numbers against the Windows ones, but only a person at a Mac
  can say whether the two windows look the same
- the bundle is signed ad hoc and checked by `tools/check_mac_bundle.py`,
  but it is not notarized, so the first launch asks once by design
- the app is not in the Mac App Store

## The gaps that were closed

The gaps that were listed before are closed. Here is what each one
came down to:

- **The table commands** were in the Windows application class. They
  are in a shared module now (`src/tableedit.cpp` and
  `src/tableedit.h`), the Windows build uses it, and the mac build
  has a Table menu of its own.
- **The alignment, the highlight and the clear formatting** are there.
  The alignment of a column goes with the table commands. The
  highlights use the portable marker store, and the clear formatting
  uses the shared formatting module, so the two builds do the same
  thing.
- **Drag and drop** opens the file that is dropped on the window.
- **The file association** has a command that asks the system to make
  MarkDownIt the reader of the markdown files.
- **Printing** is not a gap. The Windows build has no print command
  either, so there is nothing to copy.

The shared marker type is called `TextMarker` now, because
CarbonCore has a `struct Marker` of its own and AppKit pulls it in.

## The spec-gap closure

Five batches then took every standing spec in `references/` and closed
what the mac build was missing, each one a push with its CI run green
(the list is in `.hermes/plans/2026-10-23_004116-macos-spec-gap-closure.md`):

- the caret model (the text spec's M rules and the word-processor
  guidelines' acceptance list), the table keyboard and paste, and the
  find bar's keys, counter and status line
- the zoom: the scroll view carries the document, and a zoom holds its
  anchor under the gesture or the viewport centre
- the mark: only drawn text is stored, the overlap test sees
  unresolved markers, the link dialog keeps the label, and Save As
  adopts the new path for the sidecar
- the format toggles follow the shared marker-run rules, and the
  pending typing format is ported, so an empty caret can carry a bold
  or an italic into the next characters
- the outline pane, the office reports and the import identity, and
  the mermaid measurement

The typography plan
(`.hermes/plans/2026-08-12_105500-markdown-layout-typography.md`)
was walked task by task; its status section lists the evidence for
each task on the mac renderer. The palette is shared
(`src/theme.h`), so the table, quote, code and text colours cannot
drift between the two builds. Night mode is not implemented on either
platform, so its spec stays out of scope for the port.

## The plan items that were decided differently

- The plan suggested a stub for DirectWrite so the mac target would
  not include `<d2d1.h>`. That does not work, because the layout cache
  holds a real `IDWriteTextLayout*` and calls into it. The seam with
  three implementations replaced the stub.
- The plan put the mac sources in one library. They are in two: the
  shared core and the application, because the application needs the
  Objective-C runtime and the core does not.
- The plan suggested the `MarkDownIt` executable. It is `MarkDownItMac`
  in the build and `MarkDownIt` in the bundle.

## The second run on a Mac and the three bugs it found

The second attempt opened the app on real hardware, and it found three
defects that only a real machine could show. All three are fixed, and
each one lived in code that had never run before:

- **The interface was drawn upside down and mirrored.** The content
  view and the outline view are flipped views, so AppKit already hands
  the drawing a top left origin with the y axis pointing down. Both
  views flipped the context a second time in `drawRect:`, which turned
  every drawing over. The extra flip is gone. `DrawText` draws through
  Core Text with a flipped text matrix now, so the glyphs stand up,
  the baseline lands where the renderer measures it, and the drawing
  and the measuring use one font.
- **The menu items were grey, including Open.** Every item was wired
  to a hand made target that only implemented `run:`. AppKit disables
  an item whose target does not respond to its action, so the whole
  menu was dead. The target and the adapter layer are gone: the items
  carry no target, AppKit walks the responder chain to the app
  delegate, and the commands were implemented there all along.
  `tools/check_mac_menu.py` fails when the menu hands an item a target
  of its own, and it checks all 52 commands.
- **A markdown file dropped on the icon crashed the app.** The first
  real measurement of the document text over-released the lines of a
  Core Text frame: `CTFrameGetLines` returns a borrowed array, and the
  code released it and then kept a line out of it, so the heap was
  corrupt before the first line was drawn. The ownership follows the
  get rules now, a kept line is retained before its frame goes away,
  and every line carries the slice of the run that sits on it, so a
  wrapped paragraph draws its continuation lines instead of drawing
  the whole run on the first line.

Smaller things from the same pass: the scroll wheel moved the document
the wrong way, the two dead `ToWide`/`ToNarrow` helpers in the settings
carried a buffer overflow, and the window now asks about unsaved
changes both when it closes and when the app quits.

The crash log from that run named a fatal signal and nothing else,
because the app had no signal handler. `mac::InstallCrashHandler()`
writes a backtrace into the same breadcrumb log now, so the next crash
names the code that faulted.

## The third run on a Mac and the crash on the first document open

The third attempt opened the app with a live menu, and the first
document open crashed inside Core Text: `EXC_BAD_ACCESS (SIGBUS)`, an
alignment fault inside `CFGetRetainCount`, called from
`CFAttributedStringSetAttribute`, called from
`mac::CoreTextEngine::MeasureWrapped`. The run loop of the measure path
set the font and the colour attributes over a range that reached past
the end of the attributed string, because the text of the run was
appended after the attribute calls. The order is the other way round
now, the text goes in first and the attributes cover it, and the
foreground colour is a `CGColor`, which is the type the attribute takes.

The deeper cause is coverage: the app layer's text engine had no test,
because it lives outside the shared suite, so nothing in CI had ever
measured a document. `tests/text_engine_mac_test.cpp` measures through
the engine now (one run, several styled runs, a wrapped run whose slices
must rebuild the text, multibyte wrapping, and offset hit tests), and it
reproduced the fault on the runner before the fix landed. It then caught
a second, older defect in the same path: a wrapped paragraph came back
as a single line, because `CTFramesetterSuggestFrameSizeWithConstraints`
with an unbounded height fits a whole paragraph into one frame, so the
"break that fits the width" it reported was the rest of the paragraph.
The line breaks come from `CTTypesetterSuggestLineBreak` now, one line at
a time, and the line object comes from the typesetter, which the engine
owns outright, so the frame handling the second run had fixed is gone
from the measure path entirely.

## The fonts, so a document lays out the way it does on Windows

The sizes and the spacing have been shared with the Windows build from
the start, but the faces were not. The theme asked for Segoe UI and
Cascadia Mono, neither of which exists on macOS, and the mac engine
answered with Helvetica Neue and Menlo. Different letterforms are the
visible part of that. The quieter part is the advance widths: they move
the line breaks, so every paragraph, table and diagram wrapped at
different words than it does on Windows, and that is the difference a
reader notices first.

The build now ships what Windows renders with, or the closest thing
that can be shipped. Selawik is Microsoft's open, metric compatible
stand in for Segoe UI: the advance widths match, so a paragraph breaks
at the same word on both systems. Cascadia Mono is the face the Windows
build asks for first, and the same files are vendored for the mac
bundle. Both are SIL OFL, and both sit in `third_party/` with their
license files and a short note on where they came from.

The engine maps the theme's Windows names onto them, in one function
next to the Core Text code that uses it. Selawik ships no italic faces,
so the engine shears an italic run itself, tan(12 degrees), the slant
the system would have synthesized, without the risk of Core Text
answering with some other family's italic. When the fonts are missing
entirely (a test binary that never registers them, a build without the
bundle resources), the engine notices that the family it got back is
not the one it asked for, falls back to the system faces, and writes
one breadcrumb to the crash log.

The faces register at launch, from `Contents/Resources/Fonts`, in
process scope: nothing is installed on the system.
`tests/text_engine_mac_test.cpp` checks the mapping, the registered
families and the shear, `tools/check_mac_bundle.py` fails when the
bundle does not carry all seven files, and the wrapping tests now
measure through Selawik on the runner, so the CI numbers are the ones a
Mac shows.

## The command strip

The mac window had only the menu bar, while the Windows window carries
the ribbon. The window now opens with the same surface: the quick
access row with Save, Undo and Redo, the Home and Edit tabs, and the
same groups and commands, drawn with the same icon files. The table in
`src/macos/ribbon_mac.mm` mirrors `src/ribbon.xml` command for command.

- The strip owns no state. A click runs the same delegate selector the
  menu item runs, and the strip asks the delegate two questions: is a
  toggle on, and is the command available.
- The Table Tools button opens a dropdown with the row, column,
  alignment and delete commands, grouped the way the markup groups
  them.
- Deleting a table is a mac command as well now, in the Table menu and
  in the dropdown. The shared table module had every other table
  command.
- One command is left out on purpose: the Windows Edit toggle that
  switches between reading and editing. The mac app has no such split,
  so there is nothing for it to switch, and
  `tools/check_mac_ribbon.py` carries the omission in its allowlist.
- The check compares the two files tab for tab, group for group and
  command for command: order, labels, toggles and icon files. CI fails
  when they drift apart.
- The strip is AppKit, so the shared test suite cannot press its
  buttons. The check tool and the build are what CI verifies.
- The icons are copied into the bundle as a set, and the bundle check
  fails when one of the 74 files the strip asks for is missing. A build
  without them would open with a text-only strip.

## The Selawik semibold face

The font tests added after the crash work caught a rendering defect on
their first run: every semibold run, which is every heading, fell back
to Helvetica Neue. The Semibold file of Selawik carries "Selawik" as
its typographic family, so Core Text reads the three files as one
family with three weights. Asking for a family named "Selawik
Semibold" never matched, and the registration check then took the
system face for every such run.

The engine now asks for the family and lets the weight of the run pick
the face: regular 0.0, semibold 0.3, bold 0.4. The test pins the three
PostScript names, so a weight that finds no face fails the run.

## The table-size picker

The Windows build opens a grid popup under the Table button: hover the
cells to choose the size, click to insert. The mac build asked for the
size in a modal alert with two number fields. That broke three rules in
section 12 of the ribbon guidelines: the picker opens below the Table
button, it must not follow the document caret, and it takes keyboard
input as well as the mouse.

`src/macos/table_picker_mac.mm` is the same grid in AppKit. It hangs
below the button's current screen bounds, clamps itself into the
screen's work area when the window sits near an edge, and closes when
the pointer leaves or Escape is pressed. Arrow keys size the grid and
Return commits. Called from the menu bar, where the Table button may sit
on another tab, it hangs below the strip at the pointer instead.

The size range and the label are written down once in
`src/tablepicker_model.h`, and the mac picker reads them. The Windows
popup in `src/app.cpp` uses the same numbers, and the ribbon check now
fails when the two drift apart. `tests/table_picker_test.cpp` covers the
range, the clamping and the label text.

The mouse, keyboard, narrow-window and multi-display cases still need a
person at a Mac. CI compiles the picker and runs the model tests, but it
cannot hover a cell or press an arrow key.

## The layout, measured with the Windows numbers

`.hermes/plans/2026-08-12_105500-markdown-layout-typography.md` was
written for the Windows renderer, and the mac renderer had answers of
its own for most of it: one fixed gap after every block instead of the
gap that belongs to the pair of blocks, table columns sized by their
content instead of equal columns with a row height per row, no rule
under the H1 and the H2, no step in the quote bar, and a zoom that
reached the viewport and the diagrams but never the layout. The mac
renderer now runs the Windows arithmetic, ported from
`src/renderer.cpp` and checked against it rule by rule.

- **The unit.** The theme writes its font sizes in points and its
  spacing in the layout unit the plan uses, and the Windows renderer
  converts every font with 96/72 when it builds its text formats. The
  mac renderer did not, so its text came out smaller than the layout
  around it and the lines broke at other words. It converts the same
  way now, and with Selawik's matching advance widths a paragraph
  breaks at the same word on both systems.
- **The spacing.** The gaps come from the same transition rule the
  Windows renderer uses: a heading gets its wide gap before it and
  its narrow gap after it, a list keeps its items tight and keeps its
  distance from the blocks around it, and a code block keeps its
  wider gap on both sides.
- **The line spacing.** Every block sets its line height and its
  baseline from the Windows numbers: the body at 1.3 and 1.24 of the
  em, a heading at 1.25 with its baseline on the top of the line, a
  code line at 1.45.
- **The column.** The text column is the window width less the theme
  padding, capped at the reading width and centered in what is left,
  so a document sits where it sits on Windows. The cap follows the
  width setting and the zoom.
- **The headings.** An H1 and an H2 carry their rule again, inside
  the room the block reserves for it, so the next block keeps the
  same distance from the rule as it does on Windows.
- **The quotes, the lists, the tables and the code.** A quote steps
  its bar inward with its nesting depth, a list keeps the marker
  gutter and its markers count (1., 2., 3.) across a run of items, a
  table splits its width into equal columns and measures every row on
  its own, a code block draws its border around the same rounded
  outline as its fill, and an inline code span gets its own face, its
  own size and its background.
- **The zoom.** The zoom scales the metrics, the fonts and the layout
  together, and the width setting is applied in the same place. The
  mermaid painters still get the unzoomed font, because they scale
  their own text, the way the Windows measure format stays unzoomed.

Two pieces of plumbing had to move for that. The text engine keeps the
lines of the current layout alive for the hit tester and drops them
when the next layout starts, so a click at the bottom of a document
finds its own line instead of the last one the engine happened to
measure; and the rectangle of a range ends at the left edge of the
character after it, the way the Windows range boxes do, which the
selection, the search marks and the code backgrounds all draw.

`tests/renderer_mac_test.cpp` drives the renderer with a text engine
that measures on a fixed grid, so those numbers run in CI on every
push: the column and its centering, the line heights, the gaps between
the blocks, the table columns and rows, the marker gutter, the rule,
the bar, the code background, the link underline, the images above
their paragraph and the zoom. What CI cannot see is whether the window
looks like the Windows window; that one still waits for a person at a
Mac.
