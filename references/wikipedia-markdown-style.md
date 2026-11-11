# Wikipedia markdown style

MarkDownIt renders markdown in one of two visual styles. GitHub is the
reference style. Wikipedia is the default and follows the look of a
Wikipedia article.

## Choosing the style

Set the environment variable `MARKDOWNIT_STYLE` before the app starts:

- `wikipedia` (default): the Wikipedia article look.
- `github`: GitHub's markdown look.

The match ignores case and spaces. An unset or unknown value gives the
default. The app reads the variable once at startup, so a change needs a
restart. The code is in `src/markdown_style.h`.

## Where the numbers come from

The [Manual of Style](https://en.wikipedia.org/wiki/Wikipedia:Manual_of_Style)
is an editorial guide. It sets no pixel values. It asks for a clear
heading hierarchy, quotations set off by indentation, and meaning that
never rests on colour alone. The measurements come from the stylesheet
that renders those articles, the Vector skin
(`skins.vector.styles.legacy` and `mediawiki.skinning`), read on
2026-11-11.

## The profile

| Element | Wikipedia | Source rule |
| --- | --- | --- |
| Body | 14 px, line-height 1.6, ink 202122 | `body` |
| H1 | 1.8em Georgia, normal weight, 1px rule | `.mw-heading1` |
| H2 | 1.5em Georgia, normal weight, 1px rule | `h2` |
| H3 | 1.2em sans, bold | `h3` |
| H4 to H6 | 1em sans, bold | `h4` to `h6` |
| Paragraph gap | 0.5em | `p` |
| Link | 3366cc, no underline | `a` |
| List | 1.6em indent, 0.1em between items | `ul`, `li` |
| Quote | 4px bar eaecf0, 32px padding, body ink | `blockquote` |
| Code block | f8f9fa, 1px border eaecf0, 1em padding | `pre` |
| Table | grid a2a9b1, header eaecf0, bold and centered, body f8f9fa | `.wikitable` |
| Thematic break | 1px a2a9b1 | `hr` |

The theme tokens live in `WikipediaMetrics()` and `WikipediaPalette()` in
`src/theme.h`. The shared layout reads the style switches from
`LayoutMetrics`, so the Windows and macOS ports follow the style without
a branch of their own.

## Known differences

- Inline code has no 1px border. It keeps the f8f9fa fill.
- Ordered lists use the same indent as bullet lists. Vector indents them 3.2em.
- Heading spacing is one value for all levels. Vector varies it per level.
- The macOS build paints white paper under this style in both
  appearances, because the ink is fixed dark.
- Visited links do not change colour.

## Tests

`tests/markdown_style_test.cpp` names both styles explicitly and checks
the profile values, the heading faces, the link underline, the thematic
break and the code card border. The test binary pins GitHub as its
active style (`MDI_PIN_STYLE_GITHUB`), so the layout goldens keep
measuring one fixed style. That means the shipped default style is
covered by the profile test, not by the other goldens.

## Capture parity

The Wikipedia rule is dark enough to read as ink in the parity capture
compare. `tools/compare_platform_captures.py` now treats a band of two
pixels or less as a rule, not as a text row.
