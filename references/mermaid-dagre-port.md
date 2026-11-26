# Mermaid dagre port: reference notes

These notes describe the native Mermaid layout code and the golden-oracle tests.

## Layout pipeline

The flowchart layout is a port of Mermaid's dagre-based pipeline. Six phases run
in a fixed order. Each phase reads and mutates the shared graph model in
`src/mermaid/model.h`.

1. **acyclic**: Break cycles by reversing a minimum set of edges. Reversed edges
   are marked so routing can restore their drawing direction.
2. **rank**: Assign each node an integer rank. Longest path gives an initial rank,
   then tight-tree processing reduces total edge length.
3. **normalize**: Split an edge spanning several ranks into a chain of dummy nodes.
   Afterward, every edge connects adjacent ranks.
4. **order**: Select a left-to-right order inside each rank to reduce crossings.
5. **coordinates**: Assign positions with a Brandes-Köpf four-pass alignment.
6. **route**: Convert edges into polylines. Dummy nodes become bend points and are
   removed from the final node list.

`LayoutFlowchart()` uses deterministic oracle-compatible label sizes.
`LayoutFlowchartWith()` uses the same pipeline after a measurement callback has
chosen node sizes. The Windows renderer calls the DirectWrite bridge and caches
the resulting layout by complete fence source and zoom.

## Default constants

| Constant | Value | Meaning |
|---|---:|---|
| `rank_sep` | 50 | Gap between ranks in DIPs |
| `node_sep` | 50 | Gap between neighbours in a rank |
| `CHAR_W` | 8.4 | Oracle label-width approximation |
| `LINE_H` | 19 | Oracle label line height |

The golden path remains deterministic. The live renderer measures labels with
DirectWrite, so browser and native glyph metrics can differ.

## Golden-oracle test pattern

The parser and layout are checked against a JavaScript oracle rather than
hand-written coordinates.

1. `tools/mermaid-oracle/` contains the Node harness for Mermaid and dagre.
2. Flowchart fixtures are `.mmd` files under `tests/mermaid/fixtures/`.
3. The oracle emits node ids, ranks, centres, edge point lists, and edge order.
4. Generated JSON is committed under `tests/mermaid/golden/`.
5. C++ tests compare structure exactly and positions within 0.5 DIP.

The GitHub Actions workflow regenerates the goldens and fails on a diff. Do not
hand-edit golden JSON. Run the scripts in `tools/mermaid-oracle/` when a fixture
or the oracle changes.

## Source layout

| Path | Contents |
|---|---|
| `src/mermaid/model.h` | Flowchart model and output types |
| `src/mermaid/parse.cpp` | Header, node, edge, chain, and subgraph parsing |
| `src/mermaid/layout_*.cpp` | Portable layout phases |
| `src/mermaid/layout.cpp` | Shared pipeline and deterministic entry point |
| `src/mermaid/layout_measure.cpp` | Callback-based measurement entry point |
| `src/mermaid/measure_dwrite.cpp` | Windows DirectWrite measurement bridge |
| `src/renderer.cpp` | Direct2D rendering and the live layout cache |
| `tools/mermaid-oracle/` | JavaScript oracle harness |
| `tests/mermaid/` | Fixtures, goldens, and C++ tests |

## State diagrams

State diagrams are rendered: a `stateDiagram-v2` fence is promoted at parse
time to `BlockKind::MermaidState`, laid out by `LayoutStateDiagram`, and drawn
by the shared painter in each port (`src/renderer_state.cpp` on Windows,
`PaintState` in `src/mermaid/painter.cpp`). Circles are `[*]`, a box
is a state, transitions are polylines with arrowheads, and the labels sit where
the layout placed them.

Flat diagrams are supported. A composite state (`state X { ... }`) makes the
layout return an error, and the fence then stays a code block rather than
rendering half a diagram.

The state oracle is `tools/mermaid-oracle/state_oracle.mjs`. It checks the stt1-5
fixtures at 0.5 DIP. Composite states and self transitions remain unsupported.

## Layout profile: ELK settings over the shared engine

Eclipse Layout Kernel is Java (elkjs is JavaScript), and this app ships
neither runtime. ELK's `layered` algorithm is the same family as the
engine here, so the profile is expressed as ELK's settings rather than a
second implementation: `src/mermaid/elk_profile.h` names the options with
ELK's ids, and `ParamsFor` maps them onto `LayoutParams`
(`spacing.nodeNodeBetweenLayers` to `rank_sep`, `nodeNode` to `node_sep`,
`edgeEdge` to `edge_sep`, `padding` to `margin`).

ELK's numbers are the default. `MARKDOWNIT_MERMAID_LAYOUT=dagre` selects
the older profile, which the golden oracle keeps using: those goldens come
from dagre-d3-es, so the engine is still compared against it on the one
profile that can be compared with it. Changing the default does not move
those goldens, because the oracle test passes dagre's parameters
explicitly and never reads the profile.

`tests/mermaid_elk_profile_test.cpp` holds both profiles to the same
contract: same nodes, same edges, distinct node centres, a routed path
for every edge, and tighter spacing under ELK.

`tests/mermaid_elk_fixtures_test.cpp` runs the shipped profile over the
chart fixtures and asserts what must hold without a golden: every node
and edge preserved, no overlapping boxes, an edge routed from its source
to its target, ranking in the direction the chart asked for, finite
geometry. The golden files stay dagre-d3-es output and are compared with
dagre's parameters named explicitly, so they keep their meaning as the
external reference.

## Validating the implementation

`tests/mermaid_validation_test.cpp` is the gate that does not depend on an
oracle. Every fixture is pushed through the real path (a fenced block, md4c,
the promotion in the parser) and checked four ways: the fence is promoted to
the diagram it claims to be, the geometry obeys its invariants (finite
numbers, nodes inside the canvas, no overlapping boxes, lanes that enclose
their members and keep off each other), two identical parses produce identical
geometry, and the geometry matches a checked-in snapshot in
`tests/mermaid/snapshot/`.

The snapshot is a regression pin, not an oracle: it says the layout did not
move, not that it is right. Regenerate it on purpose with
`MARKDOWNIT_UPDATE_SNAPSHOTS=1`, and only when the change is intended.

That suite has already earned its keep: it caught the state layout's canvas
being an oracle shim (`len * 4 + 16` by 28 points) rather than the drawing's
bounding box, which clipped every state below the first rank.

## Where a layout says a text goes

Two conventions live side by side, and mixing them is a silent bug: a node
stores its centre (`cx`, `cy` or `x`, `y` inside the box) and a painter
centres on it, while a *label box* stores its top-left corner and a painter
centres on `x + w/2, y + h/2`. The state labels are boxes; the pie, flowchart,
gantt and sequence anchors were checked against their painters and agree.

A new painter should ask which one it holds before it draws. The state label
comment says it, and the dark-mode code comment rule applies: the convention,
not the caller, decides.
