# Typography, layout and WYSIWYG editing

This reference is normative for the rendered page and for editing, in both
native builds: Win32 (`src/renderer.cpp`, `src/app.cpp`) and macOS
(`src/macos/renderer_mac.cpp`, `src/macos/app_mac.cpp`). It absorbs two
one-time plans, `.hermes/plans/2026-08-12_105500-markdown-layout-typography.md`
and `.hermes/plans/2026-08-12_140000-wysiwyg-editor.md`, and replaces them as
the standing statement of what the page looks like and how editing behaves.

Where a rule differs by platform, the text says so. Where the shipped code
answers differently than the plans did, section 6 records it.

## 1. Scope and sources

The document covers two subjects and keeps them apart:

- the page: type, spacing, colour, and the treatment of each block kind.
- editing: how a keystroke becomes a splice in the Markdown buffer, and what
  the reader sees while it happens.

Numbers do not live in this file. `src/theme.h` owns every layout number and
colour in `LayoutMetrics` and `Palette`, and both renderers read them through
`ComputeMetrics()`. Changing a value is changing that header; this document
names the role. The values quoted below are the shipped ones at the time of
writing, for orientation only.

Sources, for judgement calls:

| Source | What to take from it |
| --- | --- |
| GitHub Primer `markdown-body` | the palette, the type scale, margin collapse, the code card, the table |
| Butterick, *Practical Typography* | line length, line spacing, why not pure black |
| Bringhurst, *The Elements of Typographic Style* | the modular scale and the measure |
| `references/osx-markdown-visual-style-guide.md` | the macOS reading surface: measure, padding, semantic colour |
| Google Markdown Style Guide | authored Markdown, not rendering |

## 2. Platform map

| Area | Windows | macOS |
| --- | --- | --- |
| Page renderer | `src/renderer.cpp`, Direct2D and DirectWrite | `src/macos/renderer_mac.cpp`, Core Graphics and Core Text |
| Font request | Segoe UI, Cascadia Mono, semibold body face for headings | the same names, mapped onto the system faces, with semibold expressed as its own family name |
| Appearance | fixed light palette | semantic colours, so light and dark follow the system |
| Measure | four content-width modes with a cap | the column follows the window, with side padding |
| Commands | ribbon and accelerators | menu bar, toolbar and the responder chain |

The shared `src/theme.h`, `src/blockspacing.h` and `src/caret.h` are the same
code in both builds. A rule stated without a platform belongs to that shared
core, and a platform that answers differently must say so here and in section
6.

## 3. The page

### 3.1 One token set, one scaling pass

Every layout number and colour comes from `LayoutMetrics` and `Palette`.
Neither `Measure` nor the paint path may keep a local constant for a spacing,
a radius or an ink. `ComputeMetrics()` scales the base tokens by the current
zoom once per pass, and both paths use that one result, so margins, gaps,
radii and the width cap follow the zoom instead of staying at their 100 percent
values.

### 3.2 Type scale

Body text sets the scale; every other role is a fixed step on it. Headings
carry their level through size and weight together, never through colour
alone, and H6 is the one heading that also reads muted.

| Role | Size (pt) | Weight | Shipped ink |
| --- | --- | --- | --- |
| Body | 12 | regular | text primary |
| H1 | 24 | semibold | heading |
| H2 | 18 | semibold | heading |
| H3 | 15 | semibold | heading |
| H4 | 12 | semibold | heading |
| H5 | 10.5 | semibold | heading |
| H6 | 10.2 | semibold | muted |
| Code block and inline code | 10.2 | regular | text primary |

The scale is GitHub's: body 16 px and code 13.6 px at 96/72, with headings at
2, 1.5, 1.25, 1, 0.875 and 0.85 em. The plans proposed a 15/26/21/17/13.5/13 pt
scale; the shipped scale replaced it, and `src/theme.h` is the record.

### 3.3 Line height

Line height is a multiplier of the font size, applied by the line-spacing call
of the text engine and never left to the font's natural box: body 1.5, a
heading 1.25, a code line 1.45. A single body line height keeps paragraphs at
the same rhythm across both platforms.

### 3.4 Measure and side padding

Text never sits against the window edge, and prose never runs so wide that a
line is hard to follow.

- Windows offers four content-width modes. The widest lines are capped by
  `maxContentWidth` (800 pt), and the cap scales with zoom so a zoomed page
  keeps its characters per line.
- macOS does not consult the cap. The column follows the pane, and the side
  padding slides from the theme's 56 pt down to 16 pt below a 640 pt pane, so a
  narrow window keeps a usable measure instead of a narrow column in a wide
  margin. The visual style guide forbids forcing a fixed narrow column on every
  Mac display, and this is the recorded answer to that rule.
- With wrapping off, the text gets one wide line and the reader scrolls to its
  end. Neither platform truncates it.

### 3.5 Vertical rhythm

The gap between two blocks follows GitHub's margin collapse: the gap is the
larger of the previous block's bottom margin and the next block's top margin,
and the gap belongs to the block that follows it. The first block in the
document has no gap above it. List items are the one exception (`li + li`
adds a quarter em).

| Block | Above | Below |
| --- | --- | --- |
| Paragraph, quote, table, fenced block | none | 16 pt |
| Heading | 24 pt | 16 pt |
| Thematic break | 24 pt | 24 pt |
| List | none | 16 pt |
| List item after list item | 4 pt | |

`src/blockspacing.h` implements the rule, and both renderers call it, so a new
block kind is a case in that header and not a second copy of the numbers.

### 3.6 Colour

The palette is GitHub's current Markdown palette, in roles:

| Role | Use |
| --- | --- |
| text primary | body text; near-black, not pure black |
| muted | H6, quote text, secondary captions |
| heading | H1 to H5 |
| link | links, GitHub's link blue |
| code background, code border | fenced code card and inline code surface |
| rule, table border, quote bar | the one border grey, so rules cannot drift apart |
| table header, alternate row | the header surface and the zebra wash |
| selection, find match, current match | the selection band and the two find washes |
| marker background, marker line | the reader's highlight |

Two rules sit above the values:

- Muted text must pass the contrast floor. On macOS `secondaryLabelColor`
  measures about 3.3 to 1 over white, so the muted slot is `labelColor` at an
  alpha around 0.62 instead; it follows both appearances and clears the floor.
- A highlight is never carried by colour alone. A marked range gets the wash
  and a line along its bottom edge, and both cover drawn words only: the
  blank beside a word is never part of a mark, when it is stored or when it
  is painted.

macOS derives its palette from system colours (`SystemPalette()`), so light and
dark follow the appearance without a second palette. There is no separate
night-mode document palette on macOS, and the chrome palette is the one place
with fixed light and dark counterparts.

### 3.7 Block treatments

| Block | Treatment |
| --- | --- |
| Heading | H1 and H2 draw a rule below the text, at a ratio of the heading size |
| Fenced code | a rounded card: `codeRadius` 6 pt, `codePad` 16 pt, code background and a hairline border |
| Inline code | a chip on the same surface, 0.4 em of horizontal padding and a rounded cap, on the text baseline; a mark on the span is painted above the chip so it covers the characters only |
| Quote | a 4 pt bar per depth plus 20 pt indent per depth, muted text, no background tint |
| List | marker at 0.6 times the indent plus the marker gutter, text edge on the indent |
| Table | header surface, zebra wash on alternate rows, hairline borders, real measured row heights |
| Thematic break | one hairline in the rule colour, 24 pt above and below |

Four answers here are recorded divergences from the plans, and section 6.2
keeps them: no quote background tint, equal table columns, the shared list
marker formula, and real row measurement at draw time rather than a separate
measurement call.

## 4. Editing

### 4.1 Markdown stays the source of truth

The Markdown buffer is the document. A keystroke becomes a splice on that
buffer; the parse tree, the layout and the caret are derived state. Nothing
writes the parse tree directly, no edit reserialises a block the reader did not
touch, and a save writes back the bytes that were spliced, so a construct the
reader never edited keeps its original spelling (`*` versus `_`, setext versus
ATX, a hard break by spaces versus backslash).

The consequence is that every edit reparses. Section 6.3 records the budget
that still misses.

### 4.2 The caret is one canonical position

One position type (`src/caret.h`) is the only representation of a caret or a
selection. Mouse hit-testing, keyboard movement, clipboard edits, undo and
find all read and write it. Hit-testing yields a candidate from the platform's
text engine; it never decides a paragraph, a cell or a Markdown construct on
its own. A visual position and a logical position can differ at a line wrap or
an RTL boundary, and the visual side is layout data, not model state.

### 4.3 Plain typing never formats

Formatting comes from a command. Typing Markdown syntax is text.

- A typed character that Markdown would read as syntax at that position is
  inserted in its escaped form: `*`, `_`, `` ` `` and `\` always; `#` and `>`
  at a line start; `[`, `]`, `(`, `)`, `!` and a pipe where they would open a
  construct or split a table cell.
- Inside an existing code span nothing is escaped, because the characters are
  already literal.
- Pasted plain text gets the same guard, so a stray asterisk in a pasted
  paragraph cannot become emphasis.
- No autoformat path ships. Typing `## Hello` leaves literal text. The
  binding text specification forbids automatic Markdown formatting, so the
  plans' optional Autoformat toggle is closed, not deferred.

### 4.4 Formatting is one command, one undo step

An inline marker, a heading, a list, a quote and a fenced block are applied by
one command that splices once and records one undo step. A table command
rewrites the table span in one splice, so adding a row is one undo step. A
toggle that wraps a selection places the markers inside any whitespace the
selection swept up, so emphasis opens on a word.

### 4.5 Hidden syntax is never a stop

Markdown syntax that the page does not draw is not a caret stop, not part of a
selection, and not the edge of a marker or a find match. A copy of a formatted
range carries the text the reader sees, and clear-format removes the markers
without touching the content.

### 4.6 Input methods

Composition is part of the text path, not a separate buffer.

- Windows uses the IMM32 composition path.
- macOS implements `NSTextInputClient`: marked text, `setMarkedText:`,
  `unmarkText`, `firstRectForCharacterRange` and the candidate rectangle.
- Composition over a selection replaces the selection, and the whole
  composition is one undo step, on both platforms.

### 4.7 Reading first, then editing

A file opens for reading. The Edit command turns writing on; until then every
mutation path is a no-op and the commands report themselves as unavailable.
Clipboard copy, find and selection work in both modes. There is no preference
that opens a file straight into editing.

### 4.8 Tables

A table is a structural block with cells, not text with pipes in it.

- Tab and Shift+Tab walk the cells; Tab from the last cell appends one row in
  one undo step.
- Up and Down move by row and keep the column.
- Enter inside a cell is a documented no-op, because a Markdown table cell has
  no line break.
- A typed pipe is escaped inside a cell and left alone outside one.
- Deleting the last column refuses, and the command is disabled rather than
  destroying the table.

### 4.9 Find, marks and text drag

Find and replace, the reader's highlight markers and moving a selection by
dragging shipped after the plans. They follow the binding text specification:
one search scope over the document text in every view, a plain-text paste, a
mark that begins and ends on drawn text, and a drag that is one undo step.

## 5. Evidence

| Area | Gate |
| --- | --- |
| Type, spacing, palette, block treatments | `tests/renderer_mac_test.cpp` and the Windows renderer tests; the numeric tokens are pinned in `src/theme.h` |
| Caret, selection, hit-testing | `caret_test`, `hittest_test`, `navigation_test`, `text_engine_mac_test` |
| Editing commands and undo | `formatting_test`, `undo_test`, `editor_features_test`, `table_interaction_test` |
| Plain typing and paste | `inputfilter_test`, `autoformat_test`, `composition_test` |
| Incremental parse | `incremental_test`, and the budgets in `perf_harness` |
| The page in pixels | the QA walk (`doc`, `edit`, `outline`, `find`, `wide`, `narrow`, `dark`, `tail`, `tail2`), with `tests/fixtures/layout-showcase.md` |

The shared suite runs in the Windows job and in the macOS job, the macOS job
also runs it under sanitizers, and the Windows job's x64 leg still dies in the
pre-existing `SeqGolden.ParsesSeq1` crash before it can gate anything.

## 6. Compliance

### 6.1 macOS, per area

| Area | State | Evidence |
| --- | --- | --- |
| Token set and one scaling pass | Compliant | `MacRenderer::ComputeMetrics`, `metrics_` read by both paths |
| Type scale, H6 muted | Compliant | `headingSizes`, `FontFor`, heading colour by level |
| Line height | Compliant | `bodyLineHeight` through the line-spacing call |
| Measure and padding | Compliant with the macOS rule | column follows the pane, `EffectivePadX` slides 56 to 16 below 640 pt, cap deliberately not consulted |
| Vertical rhythm | Compliant | `GapForTransition` from `src/blockspacing.h` |
| Colour, light and dark | Compliant | `SystemPalette()` from semantic colours |
| Block treatments | Compliant | rounded code card, quote bar, table header and zebra, H1 and H2 rules |
| Markdown as source of truth | Compliant | `src/textbuffer.h`, splices only, no block reserialisation |
| Canonical caret | Compliant | `src/caret.h`, `PlaceCaretAt` normalises the hit-test candidate |
| Plain typing never formats | Compliant | `src/inputfilter.cpp` on the typing and paste paths |
| One command, one undo step | Compliant | every command splices once and records one undo entry |
| Hidden syntax never a stop | Compliant | rendered-text clipping on selection, markers and find |
| Input methods | Compliant | `NSTextInputClient` in `src/macos/content_view_mac.mm` |
| Reading first | Compliant | `editing_` guards, menu validation, Edit toggle |
| Tables | Compliant | `HandleTab`, `TableAppendRow`, `Enter` no-op, pipe escaping |
| Find, marks, text drag | Compliant | find bar, `MarkerStore`, `src/textdrag.cpp` |

Details and the command-level map live in
`references/windows-standardtekstredigering-specifikation.md`, section
"Implementeringsstatus for macOS", which this table does not duplicate.

### 6.2 Recorded divergences

Six answers differ from the plans on purpose. They are decisions, not debt:

1. The measure cap and the four width modes are Windows features. macOS
   follows the pane, as its visual style guide requires, and `theme.h` says so
   at the token.
2. On Windows, `MeasureTable` serves `Measure` while `DrawTable` measures its
   rows again with the same helper code, so the merge of the two call sites
   was not made. The scrollbar accuracy the extraction targeted is fixed. The
   macOS renderer measures the rows once, in its layout pass.
3. The list marker width stays the shared formula (0.6 times the indent plus
   the marker gutter). Measuring the widest marker was not built.
4. A quote keeps the bar and the muted text, with no background tint. The
   palette has no quote background token.
5. Table columns stay equal, a recorded cross-platform parity decision, so the
   two builds cannot disagree about a document's table geometry.
6. Night mode on macOS is the semantic palette, not a second fixed palette.
   The plans treated night mode as unbuilt on both platforms. macOS follows
   the system appearance this way; Windows still paints the fixed light
   document palette and has no night mode.

### 6.3 Open items

- Incremental reparse (plan Task 12) is not implemented on either platform.
  `ParseMarkdownIncremental` ignores the edit and full-parses; on the Windows
  host that measured about 463 ms per keystroke on a 1 MB document against a
  5 ms budget. Small documents are unaffected. The budget is an expected
  failure in `tests/perf_harness.cpp`, so it cannot be quietly deleted.
- Accessibility (plan Task 21) is the one wholly open requirement. No
  `ITextProvider` and no high-contrast detection exist on Windows; macOS
  exposes the outline and the table cells only.
- Two performance budgets are unmeasured on both platforms: full relayout on
  resize, and scrolling one page.
- The round-trip test the plan asked for is moot under the splice model: there
  is no serialiser to round-trip through. The invariant that matters, that an
  edit leaves untouched bytes alone, is covered by the splice and undo tests.

## 7. Changing a value

Change `src/theme.h`, not a renderer, and expect three checks: the renderer
tests, the QA captures, and the chrome gate if the value belongs to chrome.
A new block kind adds a case to `src/blockspacing.h` and a treatment row to
section 3.7. A new platform mechanism is a row in section 2 plus a record in
section 6.2 when it cannot match the shared rule.
