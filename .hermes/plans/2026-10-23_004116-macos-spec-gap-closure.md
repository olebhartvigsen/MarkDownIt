# Closing the macOS spec gaps

Plan for making every standing spec in `references/` hold for the macOS
build. Written after five read-only audits of the tree at `c1baf05`
(text editing, word processor, tables, find/replace, zoom,
highlighting, outline, night mode, office, mermaid).

Each batch below is one push with its CI run green before the next one
starts. The audit rows are the done test: a row moves to "done" only
when the code path changed and something exercises it.

Status: **done** means closed and verified in CI, **in flight** means
the push is on its way, **pending** means untouched.

## What the typography pass already closed

The audits predate the layout and typography parity work
(`f6211fe`, `cd4d7b6`, `9e253a3`). Rows that are already closed and
must not be reopened:

- Zoom reaches the layout and the renderer (find/replace audit Z1, Z5,
  Z10; table rules 33; night-mode S20 in part).
- An empty paragraph keeps a caret line (wp-guidelines F).
- Paragraph and list placeholders keep their line and height.
- The engine keeps the lines of the whole layout alive for hit testing
  (find/replace audit, engine lookup).

## Batch 1: the caret model

The blocking root cause of both text audits: a click could not place a
caret, and no key event reached the document. One canonical position
model runs through mouse, keyboard and clipboard, and the mouse hit
test never decides document semantics on its own.

Renderer half (this push):

- The visible-to-source map per block and per table cell, built the way
  `src/renderer.cpp` builds `u16ToSrc` (inline children carry their
  content offsets; escapes use the span's own end).
- `PointToOffset` (character-exact hit test, paragraph-end rule, cell
  binding, margin detection), `NormalizeToRenderedCaret`,
  `OffsetIsRendered`, `RangeIsRendered`, `ClipToRendered`,
  `StepGrapheme`, `StepWord`, `LineBoundary`, `TextBottom`.
- Caret rects and selection rects use the text's real x: list marker
  gutters, code padding, cell padding (highlighting audit, spec 8;
  word-processor table rules 28-29 partly).
- The old line-select hit test is gone; the Word-like line selection
  survives only for clicks that miss the text.

App half (this push):

- `MoveWordLeftInString` / `MoveWordRightInString` shared with the
  buffer-based moves, so double click, word deletion and Ctrl+Arrow
  cannot disagree (text spec S3, M5).
- `App::SelectWordAt` uses the shared `WordSpanAt` and snaps the ends
  to drawn text; `App::SelectParagraphAt` follows the block model
  (list markers and the next prefix stay out, the paragraph break stays
  in).
- Mouse placement: click places a caret, shift+click extends, double
  and triple click select word and paragraph, drag extends with
  autoscroll, clicks below the last line go to the document end.

Keyboard half (next push): first responder, `interpretKeyEvents`,
the blink lifecycle, `desiredX` for vertical movement, word and line
selectors, Tab and Shift+Return routing, drag autoscroll timers.

Rows this closes when both halves are green: text spec M1-M8, U1-U3,
S2, S4, S5, S6, S7, C2 (partly), Z1; wp-guidelines A, B, C, D, E, G,
H (new tests), ARCH-1, ARCH-2; table rules 5-7 and 12-13 (hit test and
arrows), 18 (selection start), 43-45, 51-52.

## Batch 2: table keyboard and paste

- Tab and Shift+Tab move cells through the shared table model, Tab in
  the last cell adds a row (table rules 9, 10, 11, 14, 37, 52).
- Backspace and Delete clamp to the cell (rule 17), Shift+Return uses
  the soft break and does not break a row (rule 37).
- Paste sanitizes into a cell and converts tab separated text into a
  table (rules 39, 40, 55; text spec C6).
- Z2: paste ends an undo coalescing run.

## Batch 3: find bar and zoom

- The find bar's height constraint is captured while hidden, so the
  bar is pinned to zero height (find/replace audit, blocking gap 1).
- Enter, Shift+Enter and Escape in the bar; the counter, the "No
  matches" text and the replacement count; buttons re-enable after a
  search; the current match scrolls into view; results refresh on edit
  and highlights clear on close (gaps 2, 4, 5, 6, 7).
- The search scope becomes drawn text through `ClipToRendered` /
  `RangeIsRendered` (spec 4, F12, F14).
- Zoom: Cmd+wheel, scroll clamping, a horizontal scroller bound to the
  content, and the axis centred when the content is smaller (zoom
  guideline 1.4, 1.6, 4; find/replace audit Z8, Z9, Z11-Z14, Z21, Z22,
  Z24).

## Batch 4: markers, links, format toggles

- Mark clips its range to drawn text before it is stored (highlighting
  spec 10; audit blocking row).
- The Mark toggle reports pressed on overlap, and an unresolved marker
  under the selection counts as marked (spec 4, 6).
- Save As updates the file path markers key off; File > New clears the
  marker layer (spec 12.7, 37).
- Cmd+K opens a link sheet; an empty destination is refused (text spec
  F6, F8).
- Format toggles: partial bold extends without nesting, Ctrl+I on bold
  text adds italic instead of stripping bold, empty caret keeps a
  pending format with no document change (text spec F3, F4, F5).
- Cmd+Shift+H marks (highlighting spec 5.1, 20).

## Batch 5: outline, office, mermaid

- Outline pane: independent scrolling, the active heading follows the
  caret and the scroll, keyboard navigation, accessibility exposure,
  empty state, tooltip for truncated headings (outline spec 2, 6, 6.1,
  13, 14, 16, 17, 22, 23).
- Office: the compatibility report is shown, an import does not keep
  the previous file path (data loss risk), PDF export options reach
  the UI (office spec, general clauses; audit blocking gaps 2, 3, 5,
  6).
- Mermaid labels are measured with Core Text in the live renderer
  (mermaid-dagre-port, measurement clause).

## Recorded as not a macOS gap

These stay as they are, with the reason on record:

- Night mode: the feature exists on neither platform (single light
  palette in `theme.h`), so it is a feature decision, not a port gap.
  The mixed-appearance state (dark chrome, light document) that a dark
  system appearance produces today is worth a follow-up of its own.
- PDF import: absent on both platforms.
- The read-only view/edit split: deliberately not ported.
- Clipboard formats: `NSPasteboardTypeString` with LF is the platform
  equivalent of CF_UNICODETEXT with CRLF; the real work is the hidden
  marker filtering (batch 1 and 4).
