# MarkDownIt Layout and Typography Overhaul Plan

> **For Hermes:** Use subagent-driven-development to implement this plan
> task-by-task. Use `wsl-windows-native-dev` for the build-swap-verify loop.
> CI (windows-2022) is the only compile gate. Write C++ via base64 through
> `execute_code`, never `write_file`.

**Goal:** Turn the current functional-but-plain Direct2D markdown rendering
into a typographically polished reading surface that stands next to GitHub,
Bear, and Typora without looking amateur.

**Architecture:** All layout numbers move into one `LayoutMetrics` struct in
a new `src/theme.h`. `Measure()` and `Render()` both read from that single
struct, so the two passes can never drift apart. Font sizes, spacing, and
colors become named tokens instead of magic numbers scattered across 620
lines.

**Tech Stack:** C++17, Direct2D 1.0, DirectWrite, existing `Renderer` class.

---

## Current context

Everything below was read from the live source at commit `262c8d7`.

### Type scale as it stands

| Role | Font | Size | Weight |
|------|------|------|--------|
| Body | Segoe UI | 14 pt | Regular |
| Code block | Consolas | 13 pt | Regular |
| Inline code | Consolas | 14 pt hardcoded | Regular |
| H1 | Segoe UI | 20 pt | SemiBold |
| H2 | Segoe UI | 18 pt | SemiBold |
| H3 | Segoe UI | 16 pt | SemiBold |
| H4 | Segoe UI | 15 pt | SemiBold |
| H5 | Segoe UI | 14 pt | SemiBold |
| H6 | Segoe UI | 14 pt | SemiBold |

### Spacing as it stands

| Constant | Value | Where |
|----------|-------|-------|
| `kPadX` | 48.0 | `Measure` and `Render`, duplicated |
| `kPadTop` | 24.0 | `Measure` and `Render`, duplicated |
| `kBlockGap` | 12.0 | every block, no variation |
| `kCodePad` | 10.0 | `DrawCodeBlock` |
| `kCellPad` | 8.0 | `DrawTable` |
| quote indent | `16 * (depth+1)` | both passes |
| list indent | `24 * (depth+1)` | both passes |
| list marker | 24.0 fixed | both passes |

### Color palette as it stands

| Element | Hex |
|---------|-----|
| Body text | `#000000` |
| Code text | `#333333` |
| Code block background | `#F5F5F5` |
| Inline code background | `#F0F0F0` |
| Link | `#0000CC` |
| Quote border | `#999999` |
| Thematic break | `#CCCCCC` |
| H1/H2 rule | `#DDDDDD` |
| Table border | `#CCCCCC` |
| Table header fill | `#E8E8E8` |

---

## Findings: what actually looks wrong

These are ordered by how much each one hurts the reading experience.

### F1. No measure cap, so text runs the full window width

`contentWidth = widthDip - 2 * kPadX`. Maximise on a 27 inch monitor and a
paragraph becomes a 1800 DIP line. Typographic convention puts the
comfortable range at 45 to 75 characters per line. Everything past roughly
80 characters costs the reader their place on the return sweep.

Reference: Butterick, *Practical Typography*, "Line length". Bringhurst,
*The Elements of Typographic Style*, 2.1.2.

### F2. Line height is left at the DirectWrite default

No call to `SetLineSpacing` anywhere. DirectWrite falls back to the font's
own metrics, roughly 1.15 to 1.2 for Segoe UI. Long-form reading wants 1.5
to 1.6. This single number is the biggest visual difference between the
current output and a GitHub README.

Reference: GitHub Primer sets `line-height: 1.5` on `.markdown-body`.

### F3. Uniform 12 DIP gap between every block

A heading gets the same 12 DIP above it as a paragraph does. Good typography
binds a heading to the text it introduces: large space before, small space
after. Right now an H2 floats equidistant between the paragraph it closes
and the paragraph it opens, so the document reads as an undifferentiated
column.

Reference: GitHub Primer uses `margin-top: 24px; margin-bottom: 16px` on
headings against `margin-bottom: 16px` on paragraphs.

### F4. H5 and H6 are indistinguishable from body text

Both are 14 pt, same as body. Only the SemiBold weight separates them. A
proper scale keeps every level distinct, and the conventional fix for the
smallest levels is to drop below body size and add letter-spacing or a muted
color rather than stay at parity.

### F5. Pure black on pure white

`#000000` on `#FFFFFF` is the highest contrast a screen can produce, and it
is fatiguing over a long document. Every serious reading surface softens it.

Reference: GitHub uses `#1F2328` on `#FFFFFF`. Bear and iA Writer both sit
in the same `#1A1A1A` to `#2A2A2A` range.

### F6. Inline code ignores zoom and mismatches the code block

Line 501 hardcodes `SetFontSize(14.0f * (96.0f / 72.0f))`. Two consequences:
zoom in and inline code stays put while everything else grows, and inline
code renders one point larger than a fenced block of the same language.
Inline code should also sit slightly *below* body size, because monospace
faces have a larger apparent x-height than Segoe UI at the same nominal size.

### F7. No layout constant scales with zoom

`kPadX`, `kPadTop`, `kBlockGap`, `kCodePad`, `kCellPad`, the indents, and the
marker width are all absolute DIP. Zoom to 200 percent and the text doubles
while the margins stay at 48 DIP, so the page progressively tightens.

### F8. Constants are duplicated between `Measure` and `Render`

`kPadX`, `kPadTop`, and `kBlockGap` are declared twice, once in each function
(lines 244 to 246 and 338 to 340). Any future tuning has to be applied in two
places or the scrollbar range silently desynchronises from what is drawn.

### F9. `Measure` guesses table height at 28 DIP per row

Line 288: `blockH = n.rows.size() * 28.0f`. `DrawTable` measures every cell
for real. A table with wrapped cells is taller than the guess, so the
scrollbar stops short and the last rows cannot be reached.

### F10. Blockquotes have no visual treatment beyond a grey bar

No background tint, no muted text color, and the bar is drawn at a fixed
`kPadX + 4` (line 589) so nested quotes all share one bar position instead
of stepping inward with their depth.

### F11. Code blocks have no border and no radius

A flat `#F5F5F5` fill with no edge. On white this reads as a smudge rather
than a deliberate panel. Every reference implementation adds either a 1 px
border or a rounded corner.

### F12. Tables use equal column widths

`colW = width / cols`. A table with one short column and one long one wastes
half its width. Content-proportional widths are noticeably better, and Direct2D
gives the measurements needed to compute them.

### F13. List markers are fixed at 24 DIP

A list that reaches item 10 or beyond renders `10. ` into a 24 DIP box at
body size, which clips or crowds. The marker column should be measured from
the widest marker in the list.

### F14. H1 and H2 rules sit 4 DIP under the baseline box

Line 599 draws the rule at `metrics.height + 4.0f` and adds only 5 DIP to the
cursor. The rule is optically glued to the descenders. GitHub gives it
roughly 8 px of clearance and 24 px below.

---

## Proposed approach

One new header, `src/theme.h`, holds every number and color as a token.
`Renderer` gains a `LayoutMetrics ComputeMetrics() const` that scales the
tokens by `zoom_` once per pass. Both `Measure` and `Render` call it, so F7
and F8 close together. The remaining findings become individual tuning tasks
against that struct.

### Target type scale

Sizes in points, converted with the existing `kPtToDip`.

| Role | Font | Size | Weight | Color |
|------|------|------|--------|-------|
| Body | Segoe UI | 15 pt | Regular | `#24292F` |
| H1 | Segoe UI | 26 pt | SemiBold | `#1F2328` |
| H2 | Segoe UI | 21 pt | SemiBold | `#1F2328` |
| H3 | Segoe UI | 17 pt | SemiBold | `#1F2328` |
| H4 | Segoe UI | 15 pt | SemiBold | `#1F2328` |
| H5 | Segoe UI | 13.5 pt | SemiBold | `#1F2328` |
| H6 | Segoe UI | 13 pt | SemiBold | `#656D76` |
| Code block | Cascadia Mono, fall back to Consolas | 12.5 pt | Regular | `#24292F` |
| Inline code | same family | 12.5 pt | Regular | `#24292F` |

The ratio between levels is roughly 1.22, a minor third. It keeps H1 clearly
dominant without the 32 pt jump that looks shouty in a desktop window.

Cascadia Mono ships with Windows Terminal and Windows 11. `IDWriteFactory`
falls back automatically when the family is missing, so naming it costs
nothing on a Windows 10 box that only has Consolas.

### Target spacing

All values in DIP before zoom scaling.

| Token | Value | Note |
|-------|-------|------|
| `padX` | 56 | side gutter |
| `padTop` | 32 | first block clearance |
| `maxContentWidth` | 720 | the F1 measure cap |
| `lineHeight` | 1.55 x font size | F2 |
| `paraGap` | 16 | between paragraphs |
| `headingGapBefore` | 32 | above H1 and H2 |
| `headingGapBeforeMinor` | 24 | above H3 to H6 |
| `headingGapAfter` | 10 | below any heading |
| `listItemGap` | 6 | between items in one list |
| `listGap` | 16 | around a whole list |
| `codeBlockGap` | 18 | around a fenced block |
| `codePad` | 14 | inside a fenced block |
| `codeRadius` | 6 | corner radius |
| `quoteIndent` | 20 per depth | F10 |
| `quoteBarWidth` | 4 | |
| `listIndent` | 28 per depth | |
| `cellPadX` | 12 | |
| `cellPadY` | 8 | |
| `ruleGapAbove` | 8 | under H1 and H2 |
| `ruleGapBelow` | 20 | |

### Target palette

| Token | Hex | Replaces |
|-------|-----|----------|
| `textPrimary` | `#24292F` | `#000000` |
| `textMuted` | `#656D76` | new, for H6 and quotes |
| `heading` | `#1F2328` | `#000000` |
| `link` | `#0969DA` | `#0000CC` |
| `codeBg` | `#F6F8FA` | `#F5F5F5` |
| `codeBorder` | `#D0D7DE` | new |
| `inlineCodeBg` | `#EFF1F3` | `#F0F0F0` |
| `quoteBar` | `#D0D7DE` | `#999999` |
| `quoteText` | `#656D76` | new |
| `rule` | `#D8DEE4` | `#CCCCCC` and `#DDDDDD` |
| `tableBorder` | `#D0D7DE` | `#CCCCCC` |
| `tableHeaderBg` | `#F6F8FA` | `#E8E8E8` |
| `tableRowAlt` | `#FAFBFC` | new, zebra striping |

`#0969DA` is the Primer link blue. It reads as a link at a glance and still
passes contrast against white, which `#0000CC` manages only barely.

---

## Reference implementations

Consult these when a judgement call comes up during implementation.

| Source | What to take from it | Where |
|--------|---------------------|-------|
| GitHub Primer `markdown-body` | Block spacing rhythm, heading margins, code panel treatment, table zebra striping | `https://github.com/sindresorhus/github-markdown-css/blob/main/github-markdown.css` |
| Butterick, *Practical Typography* | Line length, line spacing, why not to use pure black | `https://practicaltypography.com/` |
| Bringhurst, *The Elements of Typographic Style* | Modular scale reasoning, 2.1.2 on measure | book, chapter 2 and 3 |
| Tufte CSS | Restraint in rules and margins, generous side gutter | `https://edwardtufte.github.io/tufte-css/` |
| iA Writer | Monospace-adjacent reading surface, muted palette | `https://ia.net/topics/a-typographic-christmas` |
| Primer color tokens | The exact hex values used above | `https://primer.style/foundations/color` |
| Google Markdown Style Guide | Already mandated in AGENTS.md, governs authored markdown not rendering | `https://google.github.io/styleguide/docguide/style.html` |

The palette above is deliberately Primer-derived. Users compare a markdown
viewer against GitHub whether or not that is fair, so matching the reference
they already carry in their head is the cheapest path to "this looks right".

---

## Task breakdown

Each task is one commit. Verify through CI, then deploy and eyeball on
Windows, because none of this is verifiable from WSL.

### Task 1: Create `src/theme.h` with the token struct

**Objective:** One place that owns every layout number and color.

**Files:**
- Create: `src/theme.h`

**Step 1:** Define `struct LayoutMetrics` with every spacing token from the
table above as a `float`, and `struct Palette` with every color as a
`D2D1::ColorF` compatible `UINT32`.

**Step 2:** Add `LayoutMetrics BaseMetrics()` returning the unscaled defaults
and `LayoutMetrics ScaleMetrics(const LayoutMetrics&, float zoom)` that
multiplies every spatial field by zoom. Leave `maxContentWidth` scaled too,
so a zoomed page keeps its character count per line.

**Step 3:** Commit.

```bash
git add src/theme.h
git commit -m "feat: layout and color tokens in theme.h"
```

**Verification:** CI green. No behaviour change yet, nothing includes it.

---

### Task 2: Route `Measure` and `Render` through `ComputeMetrics`

**Objective:** Close F7 and F8 in one move.

**Files:**
- Modify: `src/renderer.h` (add `LayoutMetrics ComputeMetrics() const`)
- Modify: `src/renderer.cpp` lines 244 to 246 and 338 to 340

**Step 1:** Delete both sets of local `kPadX` / `kPadTop` / `kBlockGap`
declarations. Replace with `const LayoutMetrics m = ComputeMetrics();` at the
top of each function and swap every use to `m.padX` and so on.

**Step 2:** Do the same for `kCodePad` in `DrawCodeBlock` and `kCellPad` in
`DrawTable`, passing the struct in or calling `ComputeMetrics` locally.

**Step 3:** Commit and deploy.

**Verification:** CI green. On Windows the layout should look identical at
100 percent zoom. Zoom to 200 percent: margins and gaps now grow with the
text instead of staying fixed. This is the visible proof the task worked.

---

### Task 3: Line height and measure cap

**Objective:** Close F1 and F2, the two changes with the largest visual
payoff.

**Files:**
- Modify: `src/renderer.cpp` in `Init`, `Measure`, `Render`

**Step 1:** After each `CreateTextFormat` succeeds, call
`SetLineSpacing(DWRITE_LINE_SPACING_METHOD_PROPORTIONAL, 1.55f, 1.24f)` on
body and heading formats. Headings take a tighter 1.25 because large type
needs less leading. Code blocks take 1.45.

If `DWRITE_LINE_SPACING_METHOD_PROPORTIONAL` is unavailable on the SDK in CI,
fall back to `DWRITE_LINE_SPACING_METHOD_UNIFORM` with an explicit
`lineSpacing = fontSizeDip * 1.55f` and `baseline = lineSpacing * 0.8f`.

**Step 2:** In both passes, clamp the content width and centre it:

```cpp
float avail = widthDip - 2.0f * m.padX;
float contentWidth = wrapEnabled_ ? avail : 10000.0f;
if (wrapEnabled_ && contentWidth > m.maxContentWidth)
    contentWidth = m.maxContentWidth;
float originX = wrapEnabled_
    ? (widthDip - contentWidth) * 0.5f
    : m.padX;
```

Then replace every `drawX = kPadX` with `drawX = originX`. The quote bar at
line 589 must use `originX + 4.0f`, not the old `kPadX + 4.0f`.

**Step 3:** Commit and deploy.

**Verification:** Maximise on a wide monitor. The text column caps at roughly
720 DIP and sits centred with white space either side. Paragraphs breathe.
Confirm the scrollbar range still matches, because `Measure` and `Render` both
had to change identically.

---

### Task 4: Type scale and color pass

**Objective:** Close F4, F5, and F6.

**Files:**
- Modify: `src/renderer.cpp` `Init` (the `sizes[7]` array and both
  `CreateTextFormat` calls), line 501, line 346

**Step 1:** Replace `sizes[7]` with `{0, 26, 21, 17, 15, 13.5f, 13}`. Body
goes 14 to 15 pt. Code goes 13 to 12.5 pt and the family string becomes
`L"Cascadia Mono"`.

**Step 2:** Line 501, the inline code size, becomes
`m.codeFontDip` derived from the same 12.5 pt token times `zoom_`. Set the
family to the same string as the code format.

**Step 3:** Replace the black brush at line 346 with `palette.textPrimary`.
Add a separate heading brush and a muted brush, and use the muted one for H6
and for blockquote body text.

**Step 4:** Commit and deploy.

**Verification:** H5 and H6 are now visibly smaller than body. Zoom in and
inline code grows in step with surrounding text. Text reads as very dark grey
rather than pure black.

---

### Task 5: Differentiated block spacing

**Objective:** Close F3 and F14, the heading rhythm.

**Files:**
- Modify: `src/renderer.cpp` in both `Measure` and `Render`

**Step 1:** Replace the single `curY += blockH + kBlockGap` with a helper
that picks the gap from the block kind and the kind that preceded it:

- Heading H1 or H2, not the first block: add `m.headingGapBefore` before.
- Heading H3 to H6, not the first block: add `m.headingGapBeforeMinor`.
- After any heading: `m.headingGapAfter`.
- List item following another list item at the same depth: `m.listItemGap`.
- First or last item of a list: `m.listGap`.
- Code block: `m.codeBlockGap`.
- Everything else: `m.paraGap`.

Apply the identical helper in both passes. Extract it as a private method so
there is one implementation.

**Step 2:** For the H1 and H2 rule, move it to
`metrics.height + m.ruleGapAbove` and advance the cursor by
`m.ruleGapAbove + m.ruleGapBelow` instead of the current flat 5.

**Step 3:** Commit and deploy.

**Verification:** A heading now sits close to the text below it and far from
the text above it. Consecutive list items tighten up. Sections read as
grouped rather than as one even column.

---

### Task 6: Fix `Measure` table height and list marker width

**Objective:** Close F9 and F13, both correctness bugs.

**Files:**
- Modify: `src/renderer.cpp` `Measure` line 288, and marker handling in both
  passes

**Step 1:** Extract the row-measuring loop from `DrawTable` into
`float MeasureTable(IDWriteFactory*, const Node&, float width)` and call it
from both `DrawTable` and `Measure`. Delete the 28 DIP estimate.

**Step 2:** For lists, measure the widest marker string in the run and use
that plus a gutter as `markerW`, instead of the fixed 24. Measure it with the
body format so the number scales with zoom automatically.

**Step 3:** Commit and deploy.

**Verification:** Open a document with a wrapping table. The scrollbar now
reaches the last row. Open a list running past item 10. The markers do not
clip and the text column stays aligned.

---

### Task 7: Code block, blockquote, and table polish

**Objective:** Close F10, F11, and F12.

**Files:**
- Modify: `src/renderer.cpp` `DrawCodeBlock`, `DrawTable`, quote handling

**Step 1:** In `DrawCodeBlock`, swap `FillRectangle` for
`FillRoundedRectangle` with `m.codeRadius`, then `DrawRoundedRectangle` in
`palette.codeBorder` at 1.0 stroke.

**Step 2:** For blockquotes, fill the block rect with a faint tint before the
text, draw the bar at `originX + depth * m.quoteIndent` so nesting steps
inward, and render the text with the muted brush.

**Step 3:** In `DrawTable`, measure the natural width of each column's
content first, then distribute `width` proportionally with a floor of roughly
15 percent per column so a narrow column stays readable. Add zebra striping
with `palette.tableRowAlt` on odd body rows.

**Step 4:** Commit and deploy.

**Verification:** Code blocks read as bordered panels. Nested quotes step
inward with one bar each. Tables give wide columns the room and alternate row
tints.

---

## Files likely to change

| File | Change |
|------|--------|
| `src/theme.h` | new, all tokens |
| `src/renderer.h` | `ComputeMetrics`, `MeasureTable`, gap helper declarations |
| `src/renderer.cpp` | the bulk of the work, all seven tasks |
| `CMakeLists.txt` | no change, `theme.h` is header-only |

`src/app.cpp`, `src/parser.cpp`, and `src/dom.h` should not need touching.
If a task appears to require a DOM change, stop and reconsider, because that
signals scope creep beyond layout.

---

## Tests and validation

The parser smoke tests do not cover rendering, and there is no pixel harness.
Validation is therefore CI plus a structured manual pass.

**Per task, from WSL:**
- CI conclusion is success
- MSVC reports 0 errors and 0 warnings
- Deployed exe MD5 matches the CI artifact
- `file` reports PE32+ x86-64 GUI

**Per task, on Windows:** each task above lists what to look at.

**Final acceptance document.** Create `tests/fixtures/layout-showcase.md`
covering every block kind in one file: H1 through H6, paragraphs of at least
five lines, nested unordered and ordered lists to three levels, an ordered
list past item 10, a fenced code block, inline code inside a paragraph, a
blockquote nested two deep, a thematic break, a table with one narrow and one
wide column and a wrapping cell, a link, bold, italic, and strikethrough.

Open that file at 100, 150, and 200 percent zoom, wrap on and wrap off,
windowed and maximised. That matrix catches every regression this plan can
plausibly introduce.

---

## Risks, tradeoffs, and open questions

**Measure and Render drift is the standing hazard.** Every task that changes
spacing has to change both passes identically or the scrollbar desynchronises
from the drawing. Task 2 exists specifically to shrink this risk, and Tasks 5
and 6 should extract shared helpers rather than duplicate logic. Treat any
copy-pasted spacing arithmetic as a defect.

**Cascadia Mono may be absent on Windows 10.** DirectWrite falls back
silently, so the risk is cosmetic only. If the fallback picks something
undesirable, name a chain explicitly and test on a clean Windows 10 image.

**Proportional line spacing may not exist on the CI SDK.** Task 3 carries a
uniform-spacing fallback for exactly this. Try proportional first, because
uniform spacing interacts badly with mixed inline font sizes.

**The 720 DIP cap is a judgement call.** It yields roughly 75 to 85
characters at 15 pt Segoe UI, at the upper end of the comfortable range.
If it reads too narrow on a large monitor, 780 is defensible. Do not exceed
820, and do not make it configurable in this pass.

**Zoom-scaling the measure cap is deliberate.** The alternative, holding the
cap fixed while text grows, means zoom shows fewer characters per line rather
than larger ones. Scaling preserves the character count, which is what a user
zooming for legibility actually wants.

**Open question: dark mode.** The token structs make a second palette cheap,
but nothing else in the app knows about themes and the ribbon would need
matching treatment. Out of scope here. `theme.h` should be shaped so a
`Palette DarkPalette()` can be added later without restructuring.

**Open question: syntax highlighting.** Out of scope. It needs a lexer and a
language map, which is a separate project of its own size.
