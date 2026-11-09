# SVG diagrams: reading an export the way a browser does

## Scope

A standalone `.svg` document and an `svg`-fenced block use the same SVG scene
on both platforms. The macOS version is the reference for Windows. Windows
must not change the Mac output to match its own renderer.

On Mac, inline Markdown images use the system image renderer. That path
stays unchanged. Windows inline SVG images already use `svg::SvgDoc`, so
they also get the shared scene. Inline images are outside this parity gate.

## One renderer, two paint adapters

`mac::SvgDoc` in `src/macos/svg_mac.cpp` is portable C++. Both app targets
compile that file. It parses the shapes and text, chooses switch branches,
reads styles, expands markers, and places the drawing in its viewport.

Windows no longer gives shapes to the Direct2D SVG parser or places labels
in a second pass. `src/svgdoc.cpp` adapts the Mac scene's drawing calls to
Direct2D and measures its labels with DirectWrite. Text uses the baseline
that the scene requests. The caller's page transform stays in place.

This shares the Mac renderer's supported SVG features and its limits. It does
not add support that the Mac version lacks. Native fonts can still have
different metrics, and glyph rasterization is not pixel-identical.

## Parity gate

`MarkDownIt.svg-parity` records drawing commands from the same scene on both
native CI runners. The `SVG drawing parity (macOS reference)` job compares
Windows with the Mac trace. Windows records calls inside its actual native
paint adapter on a private WARP target. A failed native drawing call fails
the recording. Windows screen capture stays disabled.

The gate checks seven SVG fixtures at 40, 100, and 200 percent, both at the
origin and at an offset. It compares command counts, draw order, geometry,
colors, line widths, font requests, text, and baselines. Numeric tolerance
is 0.001 user units. The text engine has fixed metrics so missing fonts cannot
hide a scene-layout error. Native Core Graphics and WARP tests also render
the same primitive fixture into private bitmaps. They compare 18 interior
pixel probes for fills, strokes, rounded corners, ellipses, open and closed
paths, and winding. Three text probes check baseline bounds and embedded
newlines. Color tolerance is two byte values; glyph ink bounds allow three
pixels for native hinting. Four arrow cases check source and destination
contact in every direction, with solid and dashed paths. The combined shaft
and marker bounds must match fixed contacts within one pixel on each platform.
The green marker is also measured on its own, so a shaft reaching the border
cannot hide an arrow tip that stops short. Both checks use fixed expectations,
even if both platforms would otherwise agree on a bad result. No bitmap is
saved and no window is opened.

Each native command recorder also runs 16 changed SVGs. They must fail for
source gaps, path overshoot, marker overshoot, and marker undershoot in all
four directions. A valid fixture must pass at every tested zoom and placement.

The fixture's marker tip and `refX` are both 10, so the tip ends at the path
endpoint. The renderer must preserve explicit SVG geometry. It must not move
an endpoint or change `refX` to snap a relation to a nearby box.

The Windows tests also check zoom, page transforms, paint order, viewBox
placement, start anchors, and short-hex and RGB label colors.

The fixtures are `ai-components.svg`, `diagram.svg`, `svg-parity.svg`,
`svg-arrow-contacts.svg`, `two-boxes.svg`, `wide-diagram.svg`, and the saved
Mermaid sequence diagram
`tests/markdown-mermaid/mermaid-diagram-1788899219877.svg`. The new parity
fixture covers arcs,
curves, dashes, markers, rounded shapes, text styles, positioned tspans, HTML
fallback labels, scaling, and an offset viewBox.

## Browser reference for the Mac renderer

The browser comparisons below describe how the Mac output was established.
A change to that behavior needs its own review. Windows parity alone is not
a reason to change it.

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
