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

State diagrams have parser, layout, and golden-test code. They are not currently
promoted from `mermaid` fences to a renderable block in the application.

The state oracle is `tools/mermaid-oracle/state_oracle.mjs`. It checks the stt1-5
fixtures at 0.5 DIP. Composite states and self transitions remain unsupported.
