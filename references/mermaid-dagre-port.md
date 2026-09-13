# Mermaid dagre port: reference notes

Internal notes for anyone extending the native Mermaid renderer or porting
another Mermaid diagram type into MarkDownIt.

## Layout pipeline

The flowchart layout is a port of Mermaid's dagre-based pipeline. Six phases
run in a fixed order. Each phase reads and mutates the shared graph model
defined in `src/mermaid/graph.h`.

1. **acyclic**: break cycles by reversing a minimum set of edges. Reversed
   edges are tagged so phase 6 can flip them back for drawing.
2. **rank**: assign each node an integer rank (layer). Longest-path from
   sources gives a valid initial ranking; tight-tree tightening reduces
   total edge length.
3. **normalize**: split any edge that spans more than one rank into a chain
   of dummy nodes, one per intermediate rank. After this phase every edge
   connects adjacent ranks, which the ordering and coordinate phases assume.
4. **order**: pick a left-to-right order within each rank that minimises
   edge crossings. Median heuristic with a few sweeps up and down.
5. **coordinates**: assign x and y pixel positions. y follows the rank,
   spaced by `rank_sep`. x uses the Brandes-Köpf style four-pass alignment
   collapsed to a single balanced result, with `node_sep` between neighbours.
6. **route**: turn each edge into a polyline. Dummy nodes from phase 3
   become interior bend points, then get removed from the node list. Edges
   reversed in phase 1 get their point list flipped so arrows point the
   right way.

## Default constants

These match Mermaid's defaults so the port produces the same topology and
the same relative geometry as the browser version.

| Constant  | Value | Meaning                                           |
|-----------|-------|---------------------------------------------------|
| rank_sep  | 50    | vertical gap between adjacent ranks, in pixels    |
| node_sep  | 50    | horizontal gap between neighbours in a rank       |
| CHAR_W    | 8.4   | average character width used for node sizing      |
| LINE_H    | 19    | line height used for node sizing and label boxes  |

`CHAR_W` and `LINE_H` are approximations of Mermaid's default web font
metrics. DirectWrite gives different real widths at draw time, so node
boxes are sized from these constants during layout and then the label is
rendered inside with actual glyph metrics. This keeps layout deterministic
and independent of the font stack.

## Golden-oracle test pattern

The parser and layout are tested against a JS oracle rather than hand
written expected values. The pattern:

1. `tools/oracle/` holds a small Node harness that imports the real Mermaid
   package and runs its parser and dagre layout on a fixture.
2. Each fixture is a `.mmd` file under `tests/mermaid/fixtures/`.
3. The harness emits a JSON blob per fixture: node ids, node ranks, node
   centres, edge point lists, and canonical edge order.
4. Blobs are committed as `tests/mermaid/golden/<name>.json`.
5. The C++ tests run our parser and layout on the same fixture, then
   compare structure exactly (ids, ranks, edge endpoints, chain topology)
   and compare coordinates within a tolerance. Tolerances live next to the
   test so a new diagram type can pick its own.

Why this shape:

- Structural fields (ranks, endpoint ids, edge ordering) must match
  exactly. If they drift, layout is wrong, not just off by a pixel.
- Coordinate fields get a tolerance because font metrics differ between
  the browser oracle and DirectWrite. The tolerance is per-fixture, not
  global, so a tight diagram can stay tight.
- Regenerating goldens is one command: `node tools/oracle/run.js`. Do not
  hand-edit them.

When porting the next Mermaid diagram type (sequence, class, state), reuse
this harness. Add fixtures under `tests/mermaid/fixtures/<type>/`, extend
the oracle to call the matching Mermaid entry point, and write structural
plus tolerant coordinate assertions the same way. Do not re-derive the
oracle pattern from scratch.

## Source layout

| Path                          | Contents                              |
|-------------------------------|---------------------------------------|
| `src/mermaid/parser.*`        | header, node, edge, chain parsing     |
| `src/mermaid/graph.*`         | portable graph model                  |
| `src/mermaid/layout_*.cpp`    | one file per pipeline phase           |
| `src/mermaid/layout.cpp`      | phase driver, public `LayoutFlowchart`|
| `src/mermaid/render_d2d.cpp`  | Direct2D drawing                      |
| `tools/oracle/`               | JS oracle harness                     |
| `tests/mermaid/`              | fixtures, goldens, C++ tests          |

## State diagram (stateDiagram-v2): empirical geometry

Verified against `tools/mermaid-oracle/state_oracle.mjs` goldens (stt1-5),
tolerance +-0.5 DIP. The state renderer uses mermaid's unified dagre wrapper
(`dagre-IE2X5DAH.mjs`), which sizes layout nodes from the shim's empty-text
bbox BEFORE the label shapes are inserted, so dagre itself sees:

- state and `[*]` start nodes: w=0, h=12
- end node (`root_end`): w=14.1383, h=23 (stateEnd path bbox)
- labeled edges get a label dummy on the middle rank with w = 8*len, h = 20
  (the shim's content-width metric, same as class labels)

Rendered geometry (what the goldens store and the C++ layer emits):

- state rect: w = 8*len + 16, h = 36, centered on the dagre position
- start/end: circle r = 7 at the dagre center; end bbox is 14.0177 x 23
- edge polylines anchor at the circle border (r=7, toward the neighbour)
  for start/end and at the node center for plain states; then the d3 Basis
  spline. No marker trim: the barb marker does not shift the path.
- edge labels sit at the label-dummy center (goldens store the center).

translateGraph emulation differs from class/flowchart:

1. The margin extremes (minx/miny) are computed AFTER the rankdir
   transforms, in final space, and they include the label-proxy boxes.
   A probe proxy at x=-48 w=16 drives minx=-56 (stt1); in LR the proxy
   drives miny (the 'yes' proxy, y=0 h=20 -> -10).
2. For LR/RL the proxy box dimensions in translateGraph space are the
   ORIGINALS (8*len x 20): dagre swaps w/h before position and swaps back
   in undo. But positionY sees the SWAPPED height (label WIDTH), so proxy
   dummy sizes are set swapped (w=20, h=8*len) before AssignCoordinates.
3. Acyclic-reversed edges have their point order flipped before anchoring
   (dagre reversePointsForReversedEdges), and the anchor source/target
   roles swap for reversed edges.
4. `LayoutParams.left_align_zero` keeps the flowchart BK post-pass alive;
   state (like class) keeps raw BK coordinates.

`calcLabelPosition` in the mermaid chunk needs a NaN filter under the shim
(the w=0 nodes make dagre border intersections NaN on straight edges);
dom-shim.mjs self-patches the chunk at startup so CI works too.
Composite states (stt2) and self transitions are declared TODO: the parser
understands them, layout rejects them with an error.

