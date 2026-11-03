# GitHub's markdown style

MarkDownIt draws documents with GitHub's markdown style on both platforms.
This note records the measurements, where they came from, and how the mac
build maps them onto AppKit's typography and colours.

Source: [github-markdown-css, light](https://github.com/sindresorhus/github-markdown-css/blob/main/github-markdown-light.css),
read on 2 November 2026. GitHub's rendered markdown is the visual reference.
The CSS file is the extract that makes the numbers checkable.

## The type scale

GitHub sets the body at 16px with a 1.5 line height. Headings use a 1.25 line
box and the browser's em scale. Code is 85% of the body, not the 12px that an
earlier rule in the stylesheet suggests; the later `font-size: 85%` wins the
cascade. At 96 DPI, 13.6px is 10.2pt.

| Role | GitHub | DIP at 1x | Theme |
|---|---|---|---|
| Body | 16px / 1.5 | 16 / 24 | `bodyFontSize = 12pt`, `bodyLineHeight = 1.5` |
| Code | 85% / 1.45 | 13.6 / 19.72 | `codeFontSize = 10.2pt`, `codeLineHeight = 1.45` |
| H1 | 2em / 1.25 | 32 / 40 | `headingSizes[1] = 24pt` |
| H2 | 1.5em / 1.25 | 24 / 30 | `headingSizes[2] = 18pt` |
| H3 | 1.25em / 1.25 | 20 / 25 | `headingSizes[3] = 15pt` |
| H4 | 1em / 1.25 | 16 / 20 | `headingSizes[4] = 12pt` |
| H5 | .875em / 1.25 | 14 / 17.5 | `headingSizes[5] = 10.5pt` |
| H6 | .85em / 1.25 | 13.6 / 17 | `headingSizes[6] = 10.2pt` |

Both renderers set the line box to `lineHeight` times the font size and centre
the glyphs in it with half leading. Windows uses
`DWRITE_LINE_SPACING_METHOD_UNIFORM`; the mac applies the same arithmetic over
Core Text lines. The older builds multiplied the font's natural box, so 1.5
meant 1.5 times the font's own line height instead of 1.5 times the em.

## What changed from the previous app style

The previous build followed the mac style guide's reading rhythm and its own
type scale. GitHub's scale is tighter and its element geometry is more
explicit. The table records the difference so a reviewer can see what moved.

| Element | Before | GitHub profile |
|---|---|---|
| Body | 13.5pt at 1.3 | 12pt at 1.5 |
| Code | 11.5pt, bordered card | 10.2pt, borderless card |
| H1 | 23pt, no rule | 24pt, 1px rule |
| H2 | 19pt, no rule | 18pt, 1px rule |
| Heading gaps | 32 above, 10 below, 24 for H3 to H6 | 24 above, 16 below, all levels |
| Paragraph gap | 16 | 16 |
| List indent | 28 | 32 to the text (2em) |
| List item gap | 4 in the theme, 6 on the mac | 4 |
| Code block | 14 padding, border | 16 padding, no border |
| Inline code | 2px padding, square | .2em and .4em padding, 6px radius |
| Table cells | 12 wide, 10 high | 13 wide, 6 high |
| Table lines | row separators | full 1px grid |
| Table header | grey fill | page white |
| Table stripe | the third, fifth, ... table rows | the second, fourth, ... table rows |
| Horizontal rule | 8 above, 20 below, 12pt band | 24 on both sides, 4px bar |
| Images | scaled up to the column | max-width 100%, never upscaled |
| Links | underline at rest | underline on hover; the app keeps the rest underline |

Three GitHub traits are recorded but not copied: content-sized table columns,
hover-only link underlines and the exact 2em list text edge. The reasons are in
the deviations section.

## Vertical rhythm

GitHub's spacing is a CSS margin collapse. A paragraph, a quote, a list, a
table and a fenced block have `margin-top: 0` and `margin-bottom: 1rem`. A
heading has 1.5rem above and 1rem below. A thematic break has 1.5rem on both
sides. The gap between two blocks is the larger of the first block's bottom
margin and the second block's top margin. `src/blockspacing.h` implements that
rule once for both renderers.

| Pair | Gap |
|---|---|
| paragraph to paragraph | 16 |
| paragraph to heading | 24 |
| heading to paragraph | 16 |
| heading to heading | 24 |
| paragraph to list | 16 |
| list to list | 4 |
| list to paragraph | 16 |
| paragraph to code block | 16 |
| code block to paragraph | 16 |
| paragraph to thematic break | 24 |
| thematic break to paragraph | 24 |

`li + li` is the one exception: .25em, 4 DIP at the 16px body, between items
in one list. It is not a collapsed margin.

GitHub indents a list by 2em, so the text edge sits at 32 DIP. The app sets
`listIndent = 20` and reserves a 16 DIP marker box, which puts the text edge
at 36 DIP. The extra 4 DIP is a deliberate gutter so a two-digit marker stays
clear of the first word.

## Element geometry

### Headings

H1 is 2em, H2 1.5em, H3 1.25em, H4 1em, H5 .875em and H6 .85em, all semibold
with a 1.25 line box. H1 and H2 carry a 1px rule under .3em of padding. H6 uses
the muted ink `#59636e`. The rule is part of the block's measured height, not
the gap that follows it, so the margin model stays intact.

### Code

A fenced block has 1rem of padding, a 6px radius and the `#f6f8fa` fill. It has
no border in the current GitHub style. The text is 13.6px at 1.45. An inline
span has .2em of vertical padding, .4em of horizontal padding and a 6px
radius; its fill is `#818b981f`, which is about `#f0f1f3` over white.

### Quotes

A quote has `padding: 0 1em`, a .25em left bar in `#d1d9e0`, and muted text in
`#59636e`. The app draws the bar 20 DIP left of the indented text and keeps the
bar 4 DIP wide.

### Tables

Cells get 6px above and below and 13px at the sides. Every cell has a 1px
`#d1d9e0` border, so the table is a full grid. The header row is white, not
grey. `tr:nth-child(2n)` gives the first body row the `#f6f8fa` stripe; the
header is tr 1, so the first body row is tr 2. Headers are semibold and numbers
use tabular figures.

### Task lists

A task box is a 13px square in the marker gutter. A checked task draws the
check as two strokes, so the state does not rely on a fill colour.

### Rules and images

An `hr` is a .25em bar, 4 DIP at the body size, in `#d1d9e0`, with 1.5rem above
and below. An image has `max-width: 100%`: it shrinks to the column but never
grows past its own pixels.

### Links

GitHub uses `#0969da` and underlines a link only on hover. MarkDownIt keeps the
underline at rest on purpose; see the deviations below.

## Palette

| Token | GitHub | Use |
|---|---|---|
| textPrimary | `#1f2328` | body, headings |
| textMuted | `#59636e` | quotes, H6 |
| link | `#0969da` | links |
| codeBg | `#f6f8fa` | fenced blocks, zebra rows |
| codeBorder | `#d1d9e0` | table grid, heading rules |
| inlineCodeBg | `#eff1f3` | inline code spans |
| quoteBar, rule | `#d1d9e0` | quote bar, hr |
| tableHeaderBg | `#ffffff` | header row, the page's paper |
| tableRowAlt | `#f6f8fa` | second, fourth, ... body rows |
| selectionBg | `#b4d5fe` | selection wash |

## How the two platforms map it

Windows uses Segoe UI through DirectWrite and the fixed palette. The mac uses
the system UI face through Core Text and the system fixed-pitch face for code.
Both platforms ask for the same family names in the theme; the mac text engine
maps Segoe UI to the system UI face and Cascadia Mono to the system fixed-pitch
face, so the document metrics stay platform-neutral. `mac::SystemPalette`
fills the same slots from AppKit's semantic colours: `labelColor`, `linkColor`,
`separatorColor`,
`quaternaryLabelColor`, and `windowBackgroundColor` for the code card. Dark
Mode and increased contrast therefore follow the system, which a fixed web
palette cannot do.

## Deliberate deviations from GitHub

- Static link underline. GitHub relies on colour plus a hover underline. The
  app has no link-hover state, and the Apple guide asks that a link not rely on
  colour alone, so the underline stays visible.
- No syntax colouring. Code blocks draw plain monospace text. The renderer has
  no lexer, and a half-hearted highlighter would be worse than none.
- Equal table columns. GitHub sizes columns to their content and scrolls the
  table sideways when it overflows. A native document pane has no sideways
  table scroll, and the caret and hit-test model addresses cells by column
  index. Equal columns that fit the reading measure keep every cell addressable
  and avoid a horizontal scrollbar inside a page.
- A fixed 690 DIP reading column on the mac. At 16px it is wider, in character
  terms, than the old 18px column. It stays fixed because the capture-parity
  states record the wrap geometry; changing it would move every band in the
  comparison for a reason unrelated to the style.
- Semantic colours on the mac. The web palette is fixed light. The mac palette
  maps the same roles to AppKit colours so Dark Mode works.
- A 36 DIP list text edge instead of GitHub's 32. The marker sits at 1.25em and
  the text at 2.25em, four DIP wider, so a two-digit marker stays clear of the
  first word and a wrapped line never touches the bullet.

## Where the values live

- `src/theme.h`: `BaseMetrics()` and `BasePalette()`.
- `src/blockspacing.h`: the margin collapse.
- `src/renderer.cpp`: Windows layout and paint.
- `src/macos/renderer_mac.cpp`: mac layout and paint.
- `tests/renderer_mac_test.cpp`: the geometry tests, including the heading
  rule, the 4px break, the code card and the table grid.

## Verification

`RendererMac.AppliesTheLineSpacingAndTheReadingLead` checks the 1.5, 1.25 and
1.45 line boxes. `SpacesBlocksLikeWindows` checks the collapsed gaps.
`PaintsTheHeadingRuleTheBarAndTheBreak`, `PaintsTheCodeCardWithoutABorder` and
`PaintsTheGitHubTableGrid` check the element geometry. The capture-parity job
compares the two apps' documents on every QA state. The local Linux renderer
binary reports four known `StepsSkipTheHiddenMarkers` failures from the stub
engine; they are not style regressions.
