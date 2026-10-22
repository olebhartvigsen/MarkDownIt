# The macOS port: what is there and what is not

The plan is `.hermes/plans/2026-09-12_203000-macos-port.md`. This is the
state of the port against that plan, written on October 22, 2026, when
the branch `feat/macos-port` was green on both platforms.

## The gates

| Gate | Where it runs | State |
|------|----------------|-------|
| `Tests (macOS, Apple Silicon)` | GitHub Actions, `macos-26` | 7374 checks, 0 failures, 608 passed |
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
- The window keeps its place between the runs, and the settings live
  in `~/Library/Application Support/MarkDownIt/settings.json`.
- The bundle carries the icon and the document types, and the CI
  uploads it.

## What is not there yet

These are the gaps, and each one is a real gap and not a hidden one:

1. **The outline pane.** The View menu has the command, and it says
   that the outline is not in this build. The outline itself is shared
   (`src/outline.cpp`), so it needs a pane on the mac side and the
   window has to hold it next to the document.
2. **The table commands.** Insert table, add and remove a row or a
   column, and split a cell are in the Windows application class, not
   in a shared module, so the mac build does not have them yet. They
   need to be lifted out of `src/app.cpp` first.
3. **The alignment, the highlight and the clear formatting.** The
   ribbon has them, and the shared formatting module does not have
   them either, so both builds would need them.
4. **Drag and drop.** A file dropped on the window is not opened.
5. **Printing.** The mac build has no print command.
6. **The file association.** The bundle declares the document types,
   so the system can offer the app, but there is no "set as the
   default reader" command.

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
