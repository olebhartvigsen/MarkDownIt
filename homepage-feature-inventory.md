# MarkDownIt homepage feature inventory

Merged from `references/` and `.hermes/plans/` (extracted 2026-10-08, four
docs passes) and cross-checked against `src/`. Status: shipped = doc states
current behavior AND/OR verified in `src/`; spec-only = doc/plan goal only.
This file is the source of truth for homepage copy. Rules: only shipped
items go on the homepage; spec-only items stay off.

## Shipped, user-facing

Text editing (windows-standardtekstredigering-specifikation.md, "Leveret i
editorlaget"):
- Typing at the caret, typing over a selection replaces it
- Typing Markdown characters never auto-formats (plain text stays plain)
- Enter: paragraph break; in code blocks a plain newline; in lists continues
  or ends items; in table cells a no-op
- Backspace/Delete, Ctrl+Backspace/Ctrl+Delete word erases
- Arrow keys per character and visual line; Ctrl+Left/Right word jumps
- Copy/cut/paste with CRLF normalization; Ctrl+Insert/Shift+Insert/Shift+Delete
- Undo/redo (Ctrl+Z/Y), paste/cut/format are one undo step, typing folds
- Bold Ctrl+B and italic Ctrl+I toggle selection or typing state
- Ctrl+K link dialog (keeps label, edits destination, no empty links)
- Click opens links in read mode, Ctrl+click while editing (http/https/mailto,
  anchors scroll)
- Ctrl+F find and Ctrl+H replace; Replace All is one undoable step
- Drag a selection to move text
- Tab in code blocks inserts spaces, indents lists, Shift+Tab outdents
- Ctrl+A select all; inside tables steps cell, row, table, document

Word processor mechanics (word-processor-guidelines.md):
- Enter at paragraph end creates an empty paragraph
- Click past the last character stays in the paragraph
- Shift+Enter soft line break inside a paragraph
- Backspace/Delete join paragraphs, undoable
- Enter with a selection is atomic; formatting stays balanced across Enter
- Caret valid on empty paragraphs and in an empty document

Tables (word-processor-table-guidelines.md, status 2026-09-17 / 2026-10-03):
- Insert Table with size popup, caret lands in the first header cell
- Click into a cell (empty cells too), caret can never sit on a pipe,
  Backspace/Delete never remove a divider
- Tab/Shift+Tab cell navigation; Tab in the last cell appends a row
- Left/Right stay in the cell; Up/Down move by row keeping the column
- Double-click selects the cell, triple-click the row, drag clamped to cell
- Add/remove rows and columns, single undoable steps, header protected
- Split Cell; column alignment left/center/right written to the separator
- Paste into a cell strips pipes and joins lines; pipes typed in cells are
  escaped; nested tables impossible; structure validated

Markers (highlighting-functionality.md):
- Mark with the Markers group or Ctrl+Shift+H, yellow highlighter look
- Toggle off on overlapping selection; marks are view-only, never touch the
  file (no dirty flag, other viewers see plain text)
- Persist in a sidecar keyed by document path, survive edits, undo, reload,
  move/rename (adopted by content fingerprint)
- Search highlights paint over marks; exports (PDF, DOCX) exclude marks

Zoom (zoom-guidelines.md section 7):
- 25 to 400 percent, clamped, remembered in the registry across launches
- Ctrl+plus/minus and buttons in 10-point steps, dimmed at the limits
- Ctrl+wheel zooms around the pointer; Ctrl+0 returns to 100% in place
- Zoom keeps the anchor point; above ~150% a horizontal scrollbar appears,
  Shift+wheel scrolls sideways
- View tab column widths: Standard, 960, 1600, Fixed (no wrap)
- Select/copy work in rendered, edit and source mode; source copy is verbatim
  Markdown, rendered copy is cleaned visible text

Find bar (find-replace.md section 27 + text spec status register, verified
in src/findbar and src/findstate):
- One bar docked at the bottom, Ctrl+F find mode, Ctrl+H replace mode,
  replace controls grey out in find mode, replace gated to editable views
- Match case and Whole word toggles (default off), wrap-around navigation
  (findstate.cpp wraps at both ends), whole-word matching at Unicode
  boundaries, search rejects non-grapheme-boundary candidates
(RECONCILED: the find spec lists Match case/Whole word/wrap-around as
requirements, but the text spec's implementation register ships them and the
code confirms. Still spec-only: incremental search, match counter, match
highlighting, current-match selection, regex, history, find in selection)

Rendering (viewer + typography plans, current-context facts, and tree):
- Headings H1 to H6, paragraphs, bullet/numbered lists with hanging indent,
  fenced code blocks on a grey panel, blockquotes with a bar, tables as a
  bordered grid with filled header row, horizontal rules, links in blue,
  bold/italic/inline code, images render with aspect-scaled height
- Segoe UI body, Consolas code, 48 DIP side margins, even block gaps
- Scroll, live reload when the file changes on disk (FileWatcher in app.cpp,
  cmdReload in ribbon.xml), F5 forces a reload
- Word wrap toggle (cmdWrap), unwrapped lines run past the window edge
- Drag a .md file onto the window to open (README + DragAcceptFiles in app.cpp)

Mermaid (mermaid-dagre-port.md, flowchart plan closed 2026-09-16, tree):
- Native C++ dagre port, DirectWrite labels, layout cache per fence and zoom
- Flowcharts: 4 directions, rectangle/rounded/stadium/diamond/circle nodes,
  solid/dotted/thick edges with or without arrowheads, labels, chains,
  node id reuse, simple subgraphs as flat bands
- Flowchart plan closed 2026-09-16 at 1a5c334 (ancestor of main): all DoD
  boxes checked, GUI-verified TD/TB/BT/LR/RL at 25/100/400% zoom, malformed
  fence falls back to a code block; layout cache is a bounded FIFO keyed by
  fence source plus the exact zoom bit pattern; Mermaid default palette
  (#ECECFF fill, #9370DB border) as theme tokens
- Pie charts (default palette, title, labels, legend) and sequence diagrams
  (theme-default parity: activation boxes, notes, autonumber, alt/par/critical
  tag boxes, footer box; oracle scripts pie_oracle.mjs / sequence_oracle.mjs)
- CI golden oracle against real mermaid.js output within 0.5 DIP on 9 fixtures
- Broken or unsupported fence falls back to a code block, never blanks
- NOT supported (say so if listing limits): styling directives, click
  handlers, nested subgraphs, class and state diagrams (code exists, not
  wired to fences)

Office (office-import-export-spec.md + tree; office milestone 1, partial):
- Import .docx: text, headings, lists, tables, images, hyperlinks, page setup
- Export .docx: checked in CI by an independent reader (tools/office-oracle)
- Export PDF: layout, page breaks, footers
- CompatReport implemented: warns about what cannot be carried
- NOT in the app: PDF import, OCR, password-protected PDF, PDF/A,
  legacy .doc writing (deferred to a later milestone)

Distribution (release-pipeline.md, release.yml + installer/ in tree):
- Tag vX.Y.Z build attaches to the GitHub release (matrix over x64 and
  arm64, added in commit 10c379a):
  MarkDownItSetup-x64.msi and MarkDownItSetup-arm64.msi (per-user installer
  into %LOCALAPPDATA%\MarkDownIt, shortcuts, closes running instance, launches
  after install), MarkDownIt-x64.msix and MarkDownIt-arm64.msix (full-trust
  Store packages, Identity Hartvigsen.MarkDownIt, Store registers .md).
  No raw exe is attached to releases; the exe exists only inside the
  installers and as a CI artifact.
- Dual arch (x64 + arm64), no winget in any doc
- First release cut 2026-10-09: v0.2.0 "MarkDownIt 0.2 Beta", flagged
  pre-release, assets exactly MarkDownItSetup-{x64,arm64}.msi and
  MarkDownIt-{x64,arm64}.msix (1.4-1.7 MB each), no raw exe
- releases/latest 404s while the only release is a pre-release, so the
  homepage download buttons link by tag (v0.2.0) and name the version in
  copy; bump the page links and version text on every new tag
- app.rc version on main is 0.2.0; a workflow_dispatch dry-run builds all
  artifacts without attaching them (used to prove a version bump compiles)
- Microsoft Store submission is a manual Partner Center step; no listing is
  live, so never claim Store availability on the homepage

## Spec-only, must stay OFF the homepage

- Night mode (whole doc unticked, no code)
- Cursor blink behavior details (spec; caret drawing partially verified)
- Home/End, PageUp/PageDown, Shift+navigation semantics (spec in text spec)
- Find: incremental, match counter, match highlighting, wrap-around nav,
  regex, search history, find in selection
- Marker undo, context menu entry, marker navigation/counter/hover
- Table: merge cells, whole-column selection, vertical cell alignment,
  column width drag, width modes, border controls, cell background,
  table-to-text, spreadsheet paste
- Incremental reparse (typing reparses the whole file, 463 ms measured)
- Serializer round-trip guarantee, save/dirty prompts, conflict prompt
- IME composition, UI Automation accessibility, high contrast
- Ribbon: Quick Access Toolbar, Design/Layout/Review tabs, contextual tabs,
  galleries, scaling, enhanced tooltips, KeyTips (partial)
- Typography overhaul: line spacing 1.55, measure cap, Primer scale, Cascadia
  Mono, soft colors, zebra tables
- macOS port (analysis only), PDF import/OCR, winget
- Single-instance and crash-recovery promises (no code found)

## Sourcing notes

- Plans understate the tree in two places (verified in src): file drag-drop
  and images render even though the viewer plan defers them; trust README +
  tree over a stale plan.
- The WYSIWYG plan rejects a split source view; the README's "switch between
  rendered Markdown and source view" and zoom doc's source mode confirm a
  toggle exists. Claim a toggle, never a split view.
- "No network" claims must stay scoped to Mermaid rendering: imagehelper.cpp
  fetches remote images over winhttp.
