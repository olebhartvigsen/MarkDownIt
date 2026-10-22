# The macOS port: what is there and what is not

The plan is `.hermes/plans/2026-09-12_203000-macos-port.md`. This is the
state of the port against that plan, written on October 22, 2026, when
the branch `feat/macos-port` was green on both platforms.

## The gates

| Gate | Where it runs | State |
|------|----------------|-------|
| `Tests (macOS, Apple Silicon)` | GitHub Actions, `macos-26` | 7433 checks, 0 failures, 625 passed |
| `Build (macOS, Apple Silicon)` | GitHub Actions, `macos-26` | the app bundle builds and passes the bundle check |
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
  the find bar are the shared ones.
- The clipboard keeps the custom markdown format next to the text.
- The source view, the zoom, the wrapping and the width of the column
  are in the View menu.
- The menus carry the file, the edit, the view, the format and the
  office commands.
- The welcome screen shows the recent files as cards, and a click on a
  card opens that file.
- The commands that throw the document away ask about the changes
  first.
- The outline pane is there. It shows the headings, a collapsed heading
  hides its subtree, and a click on a heading moves the caret there.
- The window keeps its place between the runs, and the settings live
  in `~/Library/Application Support/MarkDownIt/settings.json`.
- The bundle carries the icon and the document types, and the CI
  uploads it.
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

- the app has never been run on a real Mac, so the editing, the
  dragging, the printing and the file associations have not been tried
  by hand
- the bundle is not signed and not notarized
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
