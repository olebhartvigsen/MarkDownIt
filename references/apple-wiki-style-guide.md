# Apple wiki style guide

The Apple style is the default look of MarkDownIt. It follows the
principles of Apple's DocC and Human Interface Guidelines: one calm sans
face, plenty of space, large headings without rules, hairlines instead
of grids, and soft surfaces with rounded corners. It borrows the
principles and none of Apple's artwork.

## Choosing the style

Set `MARKDOWNIT_STYLE` before the app starts:

- `apple` (default): this style.
- `wikipedia`: the Wikipedia article look.
- `github`: GitHub's markdown look.

The value ignores case and spaces. An unknown value gives the default.
The profile lives in `AppleMetrics()` and `ApplePalette()` in
`src/theme.h`. The shared layout reads it, so Windows and macOS draw the
same page.

## The page pattern

A page in this style has eight parts. Each one is plain markdown. The
renderer gives it a fixed look, so every wiki page reads the same way.

1. **Page header.** A `#` title, then one short paragraph that describes
   the page. The paragraph right after the title is set larger and
   muted, as the page's description. Keep it to one or two sentences.
2. **Overview.** A `## Overview` section with a short introduction. Say
   what the page covers and who needs it.
3. **Information boxes.** A quote that starts with a label word and a
   colon becomes a tinted box (see below).
4. **Architecture diagrams.** Use a fenced `mermaid` block. Diagrams use
   the style's neutral grey nodes and dark ink, so all diagrams match.
5. **Tables.** Use pipe tables. The style draws hairlines between rows
   and no vertical lines. The header is bold. Align numbers right.
6. **Code blocks.** Use a fenced block with a language. It gets a soft
   grey card with a thin border and rounded corners.
7. **Related content.** End with a `## Related` section holding a plain
   list of links. Links are blue and not underlined.
8. **Page navigation.** The outline pane builds the table of contents
   from the headings, so there is nothing to write. Keep heading levels
   in order and do not skip one.

## Information boxes

Write a quote whose first word is a label and a colon:

```markdown
> Note: A neutral hint that adds context.

> Warning: Something needs care before you go on.

> Danger: This step cannot be undone.

> Success: The change is live.
```

Leave a blank line between boxes. Lines that follow each other without
one belong to the same quote.

| Kind | Label words | Use for |
| --- | --- | --- |
| Info | `Info`, `Note` | Context and hints |
| Warning | `Warning`, `Caution`, `Important` | Risks to weigh |
| Danger | `Danger`, `Error` | Data loss and breakage |
| Success | `Success`, `Tip` | Confirmed results and good practice |

The label word stays in the text and is drawn bold in the kind's colour.
Meaning never rests on colour alone. A quote without a label word stays
a plain quote with a grey bar. Other styles draw every quote as a plain
quote, so the same file reads well in all three.

## The numbers

| Element | Value |
| --- | --- |
| Body | 17 px, line height 1.47 |
| Title (H1) | 40 px bold |
| H2 to H4 | 28, 22 and 19 px semibold |
| Description | 1.2 times the body, muted ink |
| Ink, muted ink | 1d1d1f, 6e6e73 |
| Link | 0066cc, no underline |
| Hairlines | d2d2d7 |
| Soft surface | f5f5f7 |
| Code card | 8 px radius, 1 px border e5e5ea |
| Box | 10 px radius, 16 px side and 12 px vertical padding |
| Box tints | e8f1fc, fff3e0, fdecea, e8f6ec |
| Box labels | 0055aa, 8a4a00, b00020, 1a6e2e |

The type scale follows the DocC article scale. The colours are Apple's
neutral greys. The face is Segoe UI on Windows and the system face on
macOS, so the page feels native on both.

## Accessibility

The Human Interface Guidelines ask for legible text and for meaning that
does not rely on colour. `tests/markdown_style_test.cpp` checks:

- Body, muted ink and links meet 4.5:1 on white and on every surface.
- Each box label meets 4.5:1 on its own tint.
- The four tints differ, and each box carries its label word.

## Known differences

- The page has fixed light colours. The paper is white in both system
  appearances, so the dark ink stays readable. There is no dark theme yet.
- The page header has a title and a description. Metadata lines such as
  an owner or a date have no special look. Write them as a normal
  paragraph.
- An information box is one paragraph. A second paragraph in the same
  quote is drawn as a plain quote.
- Diagrams get the style's colours. There is no frame around them.
- Headings are Segoe UI (Windows) and the system face (macOS), not SF
  Pro. SF Pro does not ship with Windows.

## Tests

`tests/markdown_style_test.cpp` covers the profile values, label
detection, the box geometry, the lead paragraph, the heading faces, the
table lines and the contrast. The test binary pins GitHub as its default
style, so the layout goldens keep one fixed style.
