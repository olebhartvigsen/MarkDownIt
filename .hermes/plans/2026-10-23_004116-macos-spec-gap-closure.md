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

Renderer half (landed in `5816224`):

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

App half (landed in `5816224`):

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

Keyboard half (landed in `8262d3b`): first responder,
`interpretKeyEvents`, the blink lifecycle, `desiredX` for vertical
movement, word and line selectors, Tab and Shift+Return routing, drag
autoscroll timers.

Rows this closes (both halves are green): text spec M1-M8, U1-U3,
S2, S4, S5, S6, S7, C2 (partly), Z1; wp-guidelines A, B, C, D, E, G,
H (new tests), ARCH-1, ARCH-2; table rules 5-7 and 12-13 (hit test and
arrows), 18 (selection start), 43-45, 51-52.

## Batch 2: table keyboard and paste (landed in `03999d3`)

- Tab and Shift+Tab move cells through the shared table model, Tab in
  the last cell adds a row (table rules 9, 10, 11, 14, 37, 52).
- Backspace and Delete clamp to the cell (rule 17), Shift+Return uses
  the soft break and does not break a row (rule 37).
- Paste sanitizes into a cell and converts tab separated text into a
  table (rules 39, 40, 55; text spec C6).
- Z2: paste ends an undo coalescing run. The mac paste is its own
  undo step now; the Windows paste path shares the merge-into-the-
  typing-run pattern (audit note), and the shared-controller fix for
  it is a follow-up.

## Batch 3a: the find bar (landed in `2ec6586`)

- The find bar's height constraint follows its visibility, so the
  strip takes its 68 points when it opens and gives them back on
  close, and the focus returns to the document (find/replace audit,
  blocking gap 1; F38). The replace half greys out in Find mode
  instead of disappearing (F37).
- Enter steps to the next match, Shift+Enter to the previous one and
  Escape closes the bar (F4, F6).
- The counter shows "3 of 17" or "No matches", the status line shows
  "14 replacements made.", and the controls re-enable through a sync
  path after every search (F8, F22, F26, F32; blocking gaps 2 and 6).
- Navigation and every refresh select the current match and scroll it
  into view; the current-match tint is re-published on navigation, the
  results refresh on every edit, and the highlights clear on close
  (F15, F16, F41, F7; blocking gaps 4 and 7).
- The search scope becomes drawn text through `RangeIsRendered`, and
  Find stays closed in welcome mode (spec 4, F12). The rect mapping
  and the table-cell highlight half of F14 was closed by the
  caret-model batch.

## Batch 3b: zoom (landed in `9676e98`)

- The scroll view does the scrolling: the document view takes the
  content's size at the current zoom, so the scrollers span the whole
  document, the horizontal axis reaches nowrap lines, and a viewport
  resize keeps the place and clamps it (zoom guideline 4 and 6.15;
  find/replace audit Z21, Z22, Z24).
- Cmd+wheel and a trackpad pinch zoom with accumulated deltas; the
  anchor point stays under the gesture, the menu and the buttons
  anchor at the middle of the viewport, and the wheel without the
  modifier scrolls as before (zoom guideline 1.3 and 1.6; Z8, Z11,
  Z12, Z13, Z14).
- Zooming and editing keep the caret in view, and the text column
  stays centered while it is narrower than the viewport (zoom
  guideline 1.4).

## Batch 4a: markers and the link sheet (this push)

- Mark clips its range to drawn text before it is stored (highlighting
  spec 10).
- The Mark toggle reports pressed on overlap, and an unresolved marker
  under the selection counts as marked (spec 4, 6).
- Save As updates the path the marker sidecar keys off; File > New
  clears the marker layer (spec 12.7, 37).
- Cmd+K opens a link sheet; an empty destination, a cancel or no
  selection changes nothing (text spec, links on a selection).
- Cmd+Shift+H marks (highlighting spec 20).

## Batch 4b: the format toggles (this push)

- The shared marker toggle follows the guide for partial selections:
  an overlap extends the run instead of nesting a second pair inside
  it, and a selection inside a run splits it at the selection, so the
  text outside keeps its formatting (text spec, fed og kursiv rules
  2, 3, 5 and 8).
- Ctrl+I on bold text adds the emphasis and keeps the bold (rule 9).
- An empty caret keeps a pending typing format for the next typed
  characters and writes nothing to the document; each toggle is one
  undo step (text spec, fed og kursiv ved et tomt caret rules 1, 2
  and 5).
- The changes live in the shared `ToggleInlineMarker`, so the Windows
  build gets the same rules; the pending model is the one the Windows
  build already carries.

## Batch 5a: the outline pane (this push)

- The pane scrolls on its own, so a long table of contents keeps every
  heading reachable (outline spec 16).
- The active heading follows the caret, and the pane scrolls it into
  view when the caret enters another section (spec 6 and 6.1). A
  document change relayouts the pane and brings the active heading
  back into view.
- The keyboard drives the pane: the arrows move the selection, Enter
  jumps to it, the left and right arrows close and open a subtree,
  and Home and End go to the edges (spec 14).
- The three states of the spec are told apart: the caret's band, the
  hover line and the selection ring, and the keyboard focus draws a
  ring around the pane (spec 13 and 22).
- An empty outline says so and says how to fill it (spec 17), and a
  row that shows a truncated heading carries the whole text as a
  tooltip (spec 16).
- Assistive technology sees the outline as a list of headings, each
  with its text, its level and whether it is the heading the caret is
  in (spec 22).

## Batch 5b: office and mermaid (next push)

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
