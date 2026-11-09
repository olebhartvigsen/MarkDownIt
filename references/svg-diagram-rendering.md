# SVG diagrams: reading an export the way a browser does

## Scope

A standalone `.svg` document and an `svg`-fenced block are read by the same
code on both platforms: `svg::ExtractTextRuns` finds the words, the platform
layer draws the shapes. The browser is the reference. Where the app and the
browser disagree, the browser wins and the difference is recorded here.

## A switch draws one branch

SVG renders the first child of a `<switch>` whose conditional attributes pass.
A reader that cannot lay out HTML still has to decide what a `<foreignObject>`
branch means, and getting that wrong is what made a draw.io export lose every
label.

The rules, implemented in `src/svgtext.cpp` and `src/macos/svg_mac.cpp`:

- A `<foreignObject>` branch is passed over, and the reader keeps the
  typography and the line breaks of the HTML inside it.
- The next branch, in a diagram export always the `<text>` fallback, is drawn
  with them.
- A branch that carries a feature test and holds nothing ends the switch. The
  draw.io notice `Text is not SVG - cannot display` sits behind an empty
  `<g requiredFeatures="..."/>`, and no browser reaches it.
- An empty `<g/>` with no feature test is treated as no content yet and the
  next branch is taken. The older exports assume that.

## A label is written twice

draw.io writes every label as HTML in a `<foreignObject>` and repeats the
words in a `<text>` fallback. The two do not always agree, so the HTML decides:

- family, size, colour, weight and slant come from the deepest element that
  declares them, which is the cascade order a browser applies
- the line breaks come from the HTML's `<div>` and `<br>`, not from the
  fallback's `<tspan>` wrap. A short label is often one HTML line and two
  tspans, and drawing the tspans produced a line the browser never shows
- a block that is centred on its box is still placed from the fallback's
  first baseline, shifted by half the difference in line count
- `line-height` is a multiplier when it is below 4 and points otherwise
- `light-dark(a, b)` resolves to `a`, the light value, because the page is
  light

The fallback's own family is `Architects Daughter`, an embedded `@font-face`
the system does not have. Trusting it drew every label in the fallback face,
which is not what the browser shows.

## Evidence

`tests/fixtures/ai-components.svg` is the export this was measured on: 44
labelled boxes in three columns, every label in a `<switch>`, every fill
doubled with `light-dark()`.

- `tests/svgtext_test.cpp` covers the three switch rules and the HTML line
  breaks, and runs in the CI test job on both platforms
- the QA states `svg-drawio`, which fits the whole drawing to the window, and
  `svg-drawio-100`, which shows it at 100 percent, capture the app for a
  comparison against a headless Chrome render
- the 100 percent comparison counts ink coverage within one pixel: 99.5
  percent of the browser's ink is covered by the app and 99.8 percent the
  other way, with a mean grey difference of 2.3 from glyph rasterization

## Acceptance

A diagram export is read correctly when

- no exporter notice or other fallback text appears that the browser does not
  show
- every label sits in the browser's face, size, colour and line breaks
- fills, strokes and transforms agree, including the half-pixel
  `translate(0.5, 0.5)` that keeps a hairline on the pixel grid

Compare at 100 percent. A fitted zoom cannot judge text: at 45 percent a
12 px label is 5 px tall.
