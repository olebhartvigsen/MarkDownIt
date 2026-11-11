# AGENTS.md - Instructions for AI agents in the MarkDownIt workspace

## Language (FIRM RULE)

**Only English and Danish are allowed in this workspace and chat. No other
languages whatsoever.** This applies to every reply, every file, every
commit message, every PR description, every code comment, every doc.

No exceptions. Do not use German, Swedish, Norwegian, French, Spanish,
Chinese, or any other language for any purpose here. If a user writes to
you in a third language, reply in English or Danish.

Both English and Danish are fine; pick based on context (Danish for
casual or project talk, English for code comments and technical docs).

## Project overview

MarkDownIt is a native markdown viewer and editor for Windows and macOS.
No Electron, no .NET runtime, no external dependencies shipped. One
executable per platform opens .md files and renders them with the
platform's own text and drawing stack: Direct2D and DirectWrite on
Windows, Core Graphics and Core Text on macOS.

Both ports are built from one codebase. See "Architecture" below before
you write any code.

## Tech stack

- Language: C++17 everywhere. Objective-C++ (`.mm`) only in `src/macos/`
  for AppKit glue.
- Windows: MSVC with a static runtime (/MT) for a zero-dependency
  binary, Win32 API, Direct2D 1.1, DirectWrite, the Windows Ribbon
  framework (ships in Windows 10/11).
- macOS: AppKit, Core Graphics, Core Text, Apple Silicon, built with
  CMake on GitHub's macOS runners.
- Markdown parser: md4c release-0.5.3, vendored at third_party/md4c/.
- Other vendored libraries: miniz, pugixml, pdfio (third_party/).
- Build: CMake 3.20+, Visual Studio 2022 Build Tools and Windows 10 SDK
  for Windows, Xcode toolchain for macOS.
- CI: GitHub Actions, .github/workflows/build.yml. Windows runs on
  `windows-2022`, macOS on the label in the `MACOS_RUNNER` repository
  variable (default `macos-15-arm64`).

## Architecture (BINDING)

### The rule: share first

**Every feature is built once, in shared code, behind a seam. A feature
is written per platform only when the platform genuinely owns it.**

When you add or change behavior, decide in this order:

1. Can it live in shared code with no platform header? Then it goes
   there, and both ports get it by construction. This is the default and
   the expected answer.
2. Does it need the platform for drawing, text measurement, or a system
   service? Then the shared code talks to a seam (an abstract class in
   `src/platform/seams.h`), and each platform supplies a thin adapter.
   The logic stays shared.
3. Is it native chrome or a system dialog (ribbon, menu bar, file
   panel, registry or JSON settings)? Then it stays native, and the
   shared command table and the parity ledger keep both sides honest.

Writing the same behavior twice, once for Windows and once for macOS, is
the failure this architecture exists to prevent. Two copies drift, and
the drift shows up as a bug on one platform only. If you catch yourself
copying a function from `src/app.cpp` into `src/macos/app_mac.cpp` (or
the other way), stop and move it into shared code instead.

A feature that is genuinely one-platform needs a written reason in
`references/platform-parity.md`. CI fails the build when a difference
has no row there.

### The layers

From the bottom up. Each layer talks only to the one below it and to the
seams.

1. **Shared core** (no platform headers). The parser, the document
   model, the text buffer, editing and undo, formatting, navigation,
   find state, the layout and the display list, the Mermaid engine,
   the Office bridge, the settings struct, the welcome screen.
2. **Seams** (`src/platform/seams.h`, `src/textlayout.h`,
   `src/platformservices.h`). Narrow abstract interfaces: a text engine
   to measure and place text, a render target to draw, an app shell for
   window services. They are deliberately small, so shared code has no
   way to reach for a platform API.
3. **Platform adapters.** One implementation of each seam per platform:
   `DWriteEngine` and `D2DTarget` in `src/windows/`, the Core Text
   engine and the Core Graphics target in `src/macos/`
   (`text_engine_mac.*`, `cg_target_mac.h`). Every method does the same
   thing on both sides, and `tests/seam_conformance_test.cpp` holds them
   to it. `src/textlayout_generic.cpp` is a third, approximate text
   layout for machines with neither engine, so shared code and its tests
   can build anywhere; nothing that needs exact glyph positions uses it.
4. **Native shells.** The window, the chrome, the dialogs and the
   system integration: `src/app.cpp`, `src/ribbon.cpp`, `src/main.cpp`
   on Windows; `src/macos/main_mac.mm`, `app_mac.cpp`, `menu_mac.mm`,
   `toolbar_mac.mm` on macOS. Shells call shared code. Shared code never
   calls a shell.

### The render path

The document goes through the same steps on both platforms:

1. `src/parser.cpp` (md4c) builds the flat `Document` node list
   (`src/dom.h`).
2. `layout::LayoutDocument` in `src/layout/` walks the nodes, measures
   through the text engine seam, and emits a **display list**
   (`src/layout/displaylist.h`): plain structs in seam types, draw order
   as data, no platform handles.
3. `display::Play` (`src/layout/display_player.cpp`) replays the list
   onto any `RenderTarget`.
4. The platform renderer (`src/renderer.cpp` on Windows,
   `src/macos/renderer_mac.cpp` on macOS) owns what is native: the
   target, scrolling, the caret and the platform painters for Mermaid
   and SVG. It does not own measuring or block placement.

Rules that follow from this:

- Coordinates are points (DIPs on Windows). Zoom is baked in at layout
  time. Shared layout never rounds; any snapping lives below the render
  target.
- Positions in the display list and in the `TextEngine` seam are UTF-8
  byte offsets. The layout cache seam (`src/textlayout.h`) speaks UTF-16
  code units and converts to source offsets itself.
- Selection rectangles, hit testing and caret movement follow one rule
  (`src/layout/selection_rects.h`, `src/navigation.cpp`,
  `src/layoutcache.cpp`). Do not add a per-platform shortcut.
- Spacing, sizes and colors come from named tokens in `src/theme.h`
  (`LayoutMetrics`) and the style in `src/markdown_style.h`. Do not put
  float literals into a renderer. The style (apple, github, wikipedia)
  is chosen at run time with `MARKDOWNIT_STYLE`, and the layout follows
  it on both ports.

### The edit path

- `DocumentController` (`src/documentcontroller.{h,cpp}`) owns the text
  buffer, the selection, the edit controller, the undo stack and the
  parsed document. Both ports hold aliases of its state.
- The command bodies live in the controller, by family: headings and
  lists, inline formatting, find and replace, tables, and file load and
  save. A command ends by notifying the port, which re-parses, redraws
  and syncs its menu. The controller knows no window, renderer or dialog.
- `src/commands.h` is the one command table: id, menu name, ribbon
  command, mac selector, mutating flag and accelerators. The Windows
  keyboard map and the mac menu keys read it.
- The ports keep native: dialogs, file watching, the recent list UI,
  the ribbon or menu wiring and the pickers.

### What is shared, and what is native

| Area | Shared (write once) | Native (one per platform) |
|------|---------------------|---------------------------|
| Parsing | `parser.cpp`, `dom.h`, md4c | none |
| Layout | `src/layout/*`, `layoutcache.*`, `blockspacing.h`, `theme.h`, `markdown_style.h` | none |
| Painting | display list and `display_player` | `D2DTarget` (Windows), the Core Graphics target (macOS), Mermaid and SVG painters |
| Text | `TextEngine` seam, `textlayout.h` seam, `textlayout_generic.cpp` fallback | `DWriteEngine`, `textlayout_dwrite.cpp` / `text_engine_mac.*`, `textlayout_coretext.cpp` |
| Editing | `DocumentController`, `editcontroller`, `undostack`, `formatting`, `autoformat`, `tableedit`, `navigation`, `inputfilter`, `textbuffer`, `searchreplace`, `findstate`, `markers`, `outline`, `commands.h` | keyboard and mouse event translation |
| Find | `FindBarModel` (`findbar_model.*`) | `findbar.cpp` / `findbar_mac.mm` |
| Outline | heading collection (`outline.*`) | `outlinepane.cpp` / `outline_pane_mac.*`, `outline_nsview_mac.*` |
| Welcome screen | `welcome_screen.*`, drawn through the seams | the shell hands in its target and engine |
| Diagrams | `mermaid.*`, `mermaidlayout.*`, `diagramcache.*`, `src/mermaid/*`, `svgtext`, SVG reader `src/macos/svg_mac.cpp` | measure and paint adapters |
| Office | `src/office/*` (docx and PDF import and export) | file dialogs |
| Settings | `AppSettings`, recent-file logic in `settings.cpp` | `settings_win.cpp` (registry) / `settings_mac.cpp` (JSON) |
| Clipboard | escaping, HTML converter, line ends in `clipboard.cpp` | `clipboard_win.cpp` / `clipboard_mac.mm` |
| Zoom, table picker | `zoommodel.h`, `tablepicker_model.h` | `zoomhud.h` / `zoom_hud_mac.*`, `table_picker_mac.*` |
| Chrome | the command table | ribbon (`ribbon.xml`, `ribbon.cpp`) / native menu and toolbar (`menu_mac.mm`, `toolbar_mac.mm`) |

### Adding a feature: the checklist

1. Put the logic in shared code. Pick the module that already owns the
   area (see the table above and the code map below). A new file goes
   into a group in `cmake/sources.cmake`.
2. If it draws, add display list items and play them. If it measures,
   use the `TextEngine` seam. Do not call Direct2D, DirectWrite, Core
   Graphics or Core Text from shared code.
3. If it is a command, add a row to `src/commands.h`, the ribbon command
   in `src/ribbon.xml` and the mac selector in `menu_mac.mm`, and a row
   in the command table of `references/platform-parity.md`.
4. Write the test in `tests/` and register it in both test targets in
   `CMakeLists.txt` (Windows and macOS). Use the fake engine
   (`tests/fake_text_engine.h`) for layout tests, so both runners print
   the same bytes.
5. If the feature cannot be shared, add the reason to
   `references/platform-parity.md`: a standing-difference row, or a row
   under the test or command tables.
6. Touch both sides of a twin pair in one change, or put a `Parity:`
   line in the commit message that says why one side is enough.
7. Do not put a Windows header into a shared file
   (`windows.h`, `d2d1.h`, `dwrite.h`, and the rest). The only
   exception is the guarded block in `src/theme.h`. A
   `#ifdef _WIN32` branch in a shared file is a last resort.

### What CI enforces

Drift is a build failure, not a code review remark. The
`alignment-static` job (and `tools/check_alignment.sh` locally) runs:

- `check_sources_listed.py`: every source is in the manifest or a
  target list, and each group stays on its declared surfaces.
- `check_parity_ledger.py`: a difference between the ports has a row in
  `references/platform-parity.md`.
- `check_command_coverage.py`: every ribbon command has a mac menu or
  toolbar entry, or a ledger row.
- `check_shared_guards.py`: no unguarded Windows header in a shared
  source.
- `check_mac_sources.py`, `check_capture_policy.py`.
- `check_paired_changes.py` (advisory): warns when one side of a twin
  pair changes alone.

The test jobs add `check_test_inventory.py` (a suite that exists on one
platform only needs a ledger row), transcript parity (the editing
transcripts in `tests/transcripts/` must give the same digest on both
runners), SVG drawing parity, and capture parity (screenshots compared
across platforms; thresholds do not get loosened).

### What is still written twice

These pairs are the known leftovers. Do not add to them. When you touch
one, look for a part you can move into shared code.

- App shells: `src/app.cpp` against `src/macos/app_mac.cpp` and
  `main_mac.mm`.
- Renderer shells and Mermaid painters: `renderer.cpp`,
  `renderer_pie.cpp`, `renderer_sequence.cpp` against `renderer_mac.cpp`
  and `mermaid_painter_mac.cpp`.
- Chrome, settings, clipboard, find bar and outline pane widgets.

Open migration steps and the reasoning behind them are in
`.hermes/plans/2026-11-09_103000-port-alignment.md` and
`references/batch4-controller-extraction.md`. Standing differences are in
`references/platform-parity.md`.

## Code map

### src/ (shared code, top level)

| File or group | What it holds |
|---------------|---------------|
| `parser.*`, `dom.h` | md4c bridge and the flat node list |
| `layout/` | shared block layout, display list, player, selection rectangles |
| `layoutcache.*`, `textlayout.h` | per-block layout cache and the UTF-16 text layout seam |
| `theme.h`, `markdown_style.h`, `blockspacing.h` | tokens, the three styles, block gaps |
| `textbuffer.*`, `editcontroller.*`, `undostack.*`, `caret.h` | the edit surface |
| `documentcontroller.*`, `platformservices.h`, `commands.h` | the shared command bodies and the command table |
| `formatting.*`, `autoformat.*`, `inputfilter.*`, `tableedit.*`, `navigation.*`, `markers.*`, `textdrag.*` | editing behavior |
| `searchreplace.*`, `findstate.*`, `findbar_model.*`, `unicode_fold*`, `find_strings.h` | find and replace |
| `outline.*` | heading list for the outline pane |
| `documentfile.*`, `clipboard.*`, `settings.*`, `textenc.h` | file decoding, clipboard logic, settings struct, text encoding |
| `welcome_screen.*` | the welcome screen, drawn through the seams |
| `svgtext.*`, `svgtypes.h`, `src/macos/svg_mac.cpp` | the SVG reader shared by both ports |
| `mermaid.*`, `mermaidlayout.*`, `diagramcache.*`, `mermaid/` | Mermaid parsers, layouts, the dagre port, the diagram cache |
| `office/` | docx and PDF import and export, the markdown bridge |
| `zoommodel.h`, `tablepicker_model.h` | the logic behind the zoom HUD and the table picker |
| `platform/seams.h` | the text engine, render target and app shell interfaces |
| `textlayout_generic.cpp` | the approximate text layout for machines with no text engine |

### Windows only

| File or group | What it holds |
|---------------|---------------|
| `src/main.cpp`, `app.*` | entry point and `AppWindow` (the Win32 shell) |
| `src/ribbon.*`, `ribbon.xml`, `resource.h` | the ribbon |
| `src/renderer.*`, `renderer_pie.cpp`, `renderer_sequence.cpp` | Direct2D renderer shell and painters |
| `src/windows/` | `DWriteEngine`, `D2DTarget` (the seam adapters) |
| `src/textlayout_dwrite.cpp` | the DirectWrite text layout |
| `src/svgdoc.*` | the DirectWrite SVG scene |
| `src/findbar.*`, `outlinepane.*`, `zoomhud.h` | Win32 find bar, outline pane and zoom HUD |
| `src/settings_win.cpp`, `clipboard_win.cpp` | registry settings and Win32 clipboard |
| `src/filewatch.*`, `fileassoc.*`, `imagehelper.*`, `colortext.*` | file watch, file association, image decode, color text |
| `src/mermaid/measure_dwrite.cpp` | DirectWrite measurement for Mermaid |
| `src/app.rc`, `app.manifest`, `icons/` | resources and the version number |

### macOS only (`src/macos/`)

| File or group | What it holds |
|---------------|---------------|
| `main_mac.mm`, `app_mac.*` | the application, the window, the selectors |
| `content_view_mac.*`, `content_nsview_mac.h`, `outline_nsview_mac.*` | the AppKit views |
| `renderer_mac.*`, `mermaid_painter_mac.*` | the renderer shell and Mermaid painter |
| `text_engine_mac.*`, `cg_target_mac.h`, `fonts_mac.*`, `textlayout_coretext.cpp` (in `src/`) | the seam adapters |
| `menu_mac.*`, `toolbar_mac.*`, `table_picker_mac.*`, `zoom_hud_mac.*` | native chrome |
| `findbar_mac.*`, `outline_pane_mac.*` | find bar and outline pane |
| `settings_mac.*`, `clipboard_mac.*`, `settings_window_mac.*` | JSON settings, pasteboard, settings window |
| `image_mac.*`, `image_draw_mac.*`, `pdf_ocr_mac.*` | image and PDF services |
| `system_theme_mac.*`, `crash_trace_mac.*`, `strings_mac.h` | platform services |

### Elsewhere

| Path | Purpose |
|------|---------|
| `cmake/sources.cmake` | the one source manifest; groups declare which surfaces may use them |
| `CMakeLists.txt` | the Windows exe, the mac core library and app, both test targets |
| `tests/` | `*_test.cpp` suites, `gtest_lite.h`, `fake_text_engine.h`, fixtures, transcripts, Office goldens |
| `tools/` | CI checkers (`check_*.py` with their `test_check_*.py`), oracles, smoke scripts, icon rendering |
| `installer/` | WiX MSI project, MSIX manifest and assets, mac installer assets |
| `docs/` | the project site (GitHub Pages) and the architecture diagrams |
| `references/` | standing design docs (see the table at the end) |
| `.hermes/plans/` | one-time task plans |
| `third_party/` | md4c, miniz, pugixml, pdfio |

## Markdown style guide

Markdown authored or reviewed in this project should follow the Google
Markdown Style Guide:
https://google.github.io/styleguide/docguide/style.html

Key takeaways: use proper list syntax (1. for ordered, - for unordered),
not bold paragraphs with manual numbering. Use semantic markdown, not
visual hacks.

## Mandatory rules

- **Branches are created only on the user's initiative.** Do NOT create a
  local or remote branch, and do NOT push to one, unless the user has
  explicitly asked for a branch in that session. Work is committed on the
  current branch and, once verified, merged into main in the same session;
  no branch is left behind. Diagnostic captures, probes and throwaway
  instrumentation are not a reason to branch either: keep them out of the
  repository, or ask before committing them.
- Humanizer skill is OBLIGATORY on all generated or reviewed text for use
  outside the chat (docs, README, commits, PR text). Load with
  skill_view(name='humanizer') and follow its 34 patterns.
- No em dashes in user-facing text. Use commas, periods, or parens instead.
- Do NOT commit or push without explicit permission from the user.
- Do NOT read or print secrets. Leave .env and credential files alone.
- Verify with `git status` / `git branch` before trusting workspace snapshots.
- **Shared code first.** Follow the Architecture section. A feature that
  works on one port only, or logic written once per port, needs a ledger
  row in `references/platform-parity.md` or it does not ship.
- **Screenshots are taken only in CI, never on a local machine.**
  `tools/check_capture_policy.py` enforces it.
- All ribbon and menu work must follow references/ribbon-menu-guidelines.md.
  Read it before changing ribbon.xml or adding, moving, or renaming tabs,
  groups, or commands, and use it as the checklist when reviewing the menu.
  That document covers the Windows window; the mac window wears native
  chrome (a standard toolbar, no ribbon) and follows
  references/macos-native-chrome.md instead.
- All text editing work must follow
  references/windows-standardtekstredigering-specifikation.md. Read it before
  changing the text model, caret, selection, mouse or keyboard input,
  clipboard, undo/redo, scroll, inline formatting (bold, italic, link),
  find/replace, Tab/Enter handling, or link activation. Treat its acceptance
  criteria as the test list for those areas, and keep behavior inside its
  scope rules (plain typing must never auto-format Markdown).
- All caret and paragraph model work must follow
  references/word-processor-guidelines.md. Read it before changing caret
  positioning, mouse hit-testing, Return/Shift+Return handling, paragraph
  splitting or merging, Backspace/Delete paragraph behavior, or the canonical
  position model. Its architectural rules are binding: one canonical caret
  representation shared by mouse, keyboard, and clipboard paths, and mouse
  hit-testing must never decide document semantics on its own. Treat its
  acceptance criteria (A-H) as the regression test list.
- All table editing work must follow
  references/word-processor-table-guidelines.md. Read it before changing the
  table document model, cell caret or selection, Tab/Shift+Tab cell
  navigation, Enter behavior in cells, or row and column operations. Tables
  are structural blocks: cell navigation follows the same canonical position
  model as the rest of the editor.
- All Office import/export work must follow
  references/office-import-export-spec.md. Read it before changing docx or
  PDF import, docx or PDF export, or the office bridge. Changes are measured
  against the CI oracle goldens (tools/office-oracle); goldens never change
  to match new output.
- Port alignment work follows references/platform-parity.md (the ledger)
  and the plan in .hermes/plans/2026-11-09_103000-port-alignment.md.
  Read the ledger before adding a source file, a command, or a test that
  exists on one platform only.

## Build and deploy

MSVC cannot run on WSL and Xcode cannot either. The compile gate is CI:
`windows-2022` for the exe and the `MACOS_RUNNER` label (default
`macos-15-arm64`) for the Mac app, see references/macos-build-pipeline.md.
Use the build-swap-verify loop from the wsl-windows-native-dev skill:

1. git push to main (triggers CI)
2. Poll gh run view until green
3. gh run download the MarkDownIt-x64 artifact
4. Copy to /mnt/c/Users/au19277/projekter/MarkDownIt/build/Release/
5. Verify MD5 matches CI artifact, file type is PE32+ GUI x86-64

Local g++ checks are fine for portable code (parser, layout, the welcome
screen, the controller and anything else that compiles without a
platform header). Run them with the fake engine and stubs, and run
`tools/check_alignment.sh` before a push. The real compile gate is
always CI, on both platforms.

## Release pipeline

Releases are cut by tag, the same way as win-dir-fan (FanFolder): bump the
version in src/app.rc, push to main, create a vX.Y.Z release there, and
.github/workflows/release.yml builds MarkDownItSetup-x64.msi and
MarkDownIt-x64.msix and attaches them to the release. The full procedure,
the Partner Center identity notes, and the post-build MSIX compliance checks
live in references/release-pipeline.md. Follow it whenever a release or a
Store submission is involved; its pitfalls (windows-2022 pinning,
Identity-scoped sed, app.rc bump before tag) are easy to rediscover the
hard way.

## Sandbox gotcha

The output layer once stripped angle brackets from file content display and
from write_file, which mangled markup files. A byte probe on 2026-10-08
showed write_file preserves angle brackets in the current container, so
markup files go through write_file directly now. If XML or RC output ever
looks mangled again, verify with a raw byte count of '<' before mass
writing, and fall back to execute_code with base64 encode/decode to get
raw bytes on disk. Verify with raw byte inspection, not the display.

## Tests

- `tests/*_test.cpp` and `tests/parser_smoke.cpp`: the suites. They run
  through `MarkDownIt.tests` on both runners (the same target name on
  Windows and macOS), built with `-DBUILD_TESTS=ON`.
- `tests/gtest_lite.h`: minimal test framework, no Google Test
  dependency. `--list` prints the inventory the parity job compares.
- `tests/fake_text_engine.h`: the fixed-width engine for layout goldens.
- `tests/seam_conformance_test.cpp`: one assertion grid over every
  engine behind the seam. A new engine or seam method needs a row.
- `tests/transcripts/`: editing transcripts that give one digest on both
  runners.
- A new suite goes into both test targets in `CMakeLists.txt`. A suite
  that can only run on one platform needs a row in the test table of
  `references/platform-parity.md`.

## Directory structure

| Path | Purpose |
|------|---------|
| src/ | shared C++ code, with the Windows shell files at the top level for now |
| src/platform/ | the seams: `seams.h` |
| src/layout/ | shared block layout, display list and player |
| src/windows/ | Windows seam adapters (DirectWrite engine, Direct2D target) |
| src/macos/ | the whole Mac port: shell, adapters, chrome, plus the shared SVG reader |
| src/mermaid/, src/office/ | the diagram engine and the Office bridge |
| cmake/sources.cmake | the one source manifest |
| third_party/ | vendored md4c, miniz, pugixml, pdfio |
| tests/ | suites, fake engine, fixtures, transcripts |
| tools/ | CI checkers and oracles, smoke scripts |
| .github/workflows/ | build.yml CI pipeline, release.yml release pipeline (MSI + MSIX) |
| installer/ | WiX MSI project, MSIX manifest with committed PNG assets, Mac installer assets |
| tools/msix-icons/ | Renders the MSIX PNG asset set from the app icon SVG (resvg + PIL) |
| docs/ | the project site and architecture diagrams |
| .hermes/plans/ | one-time task plans (viewer, typography, wysiwyg, svg/mermaid, mermaid flowchart closed, macos port, office implementation, port alignment) |
| references/ | standing design docs, listed below |

### references/

| File | Covers |
|------|--------|
| platform-parity.md | the parity ledger: standing differences, command tables, test and source exceptions |
| batch4-controller-extraction.md | the shared document controller, family by family |
| ribbon-menu-guidelines.md, macos-native-chrome.md | the Windows ribbon and the Mac chrome |
| windows-standardtekstredigering-specifikation.md, word-processor-guidelines.md, word-processor-table-guidelines.md | text editing, caret and paragraph model, tables |
| office-import-export-spec.md | docx and PDF import and export |
| apple-wiki-style-guide.md, wikipedia-markdown-style.md, github-markdown-style.md | the three styles (`MARKDOWNIT_STYLE`) |
| macos-port-status.md, macos-build-pipeline.md, macos-qa.md | the Mac port and its pipeline |
| release-pipeline.md | MSI, MSIX and the Store |
| mermaid-dagre-port.md, svg-diagram-rendering.md | the diagram engines |
| outline-pane-view.md, find-replace.md, zoom-guidelines.md, cursor-blinking.md, highlighting-functionality.md, night-mode-reference.md | single features |

## Skills

- wsl-windows-native-dev: the build-swap-verify loop.
- native-windows-editor-feature-workflow: editor features and CI review.
- reference-parity-porting: when a port must match another output.
- platform-portability-analysis: what can build on macOS.
- markdownit-ui-perf: slow find, typing or scroll.
- office-format-interop-cpp: Word and PDF import and export.
- humanizer: required for text used outside the chat.
