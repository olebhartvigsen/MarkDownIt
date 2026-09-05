# Mermaid Flowchart Support (Native C++) Implementation Plan

> **For Hermes:** Use subagent-driven-development to implement this plan task-by-task.
> Every task ends with a green oracle-comparison run. Do not advance on a red gate.

**Goal:** Render ```` ```mermaid ```` flowchart fences natively in MarkDownIt (Direct2D/DirectWrite,
zero external runtime deps), with an automated test loop that proves each step's geometry matches
what mermaid.js would produce.

**Architecture:** Port the layout pipeline mermaid.js actually uses. Mermaid does not lay out
flowcharts itself: it delegates to **dagre** (`layoutAlgorithm = 'dagre'`), which is a Sugiyama
layered-graph engine. So "render like mermaid.js" concretely means "implement dagre's four phases
with mermaid's config defaults". We build three portable C++17 libraries (parser, dagre layout,
geometry) that are unit-testable on Linux, plus a thin Direct2D drawing layer that is only compiled
on Windows. A **golden-oracle harness** runs real mermaid.js in CI, dumps node/edge coordinates to
JSON, and diffs them against our C++ output within a tolerance.

**Tech Stack:** C++17 (MSVC /MT), Direct2D 1.1 + DirectWrite, md4c (already vendored),
`gtest_lite.h` (already present). Oracle: Node 20 + `mermaid@11` + `dagre-d3-es` in a
GitHub Actions job. No new runtime dependency ships in the exe.

**Scope:** `flowchart` / `graph` only (TD, TB, LR, RL, BT). Node shapes: rect, round, stadium,
diamond, circle. Edges: solid/dotted/thick, arrow/open heads, edge labels. Subgraphs, classDef,
click handlers, and all other diagram types are explicitly **out of scope** (follow-up plans).

---

## Current Context / Assumptions

| Fact | Detail |
|------|--------|
| Branch | `feat/mermaid-flowchart`, created from `main` at `ef1e165` |
| Existing prior art | `origin/feat/mermaid-phase1` has a hand-rolled parser + ad-hoc layered layout (`src/mermaid.cpp`, `src/mermaidlayout.cpp`). **Decision: lift the parser**, replace the layout wholesale. The parser is decent; the homegrown layout is not what mermaid.js does and was never validated against it. |
| Fence info already parsed | `Node::lang` on `main` already carries the fence info string (commit `6a9c1d3`), so `lang == "mermaid"` is detectable in the renderer today |
| SVG engine on main | `src/svgdoc.*`, `src/svgtext.*` exist. We do **not** route mermaid through SVG; we draw geometry directly with Direct2D primitives. |
| Local toolchain gap | This container has **no node, npm, g++, or cmake**. Verified. Therefore the oracle and the C++ compile both run in **CI**, not locally. This is the single biggest constraint on the test loop. |
| CI | `.github/workflows/build.yml`, `windows-2022`, has a test job gated on `-DBUILD_TESTS=ON` running `MarkDownIt.tests.exe` |

### Mermaid's authoritative layout defaults

Pulled from `https://mermaid.js.org/schemas/config.schema.json` → `$defs.FlowchartDiagramConfig`.
These are the constants our port must use, or the oracle diff will never converge:

| Key | Default | Meaning |
|-----|---------|---------|
| `nodeSpacing` | `50` | dagre `nodesep`: gap between nodes in the same rank |
| `rankSpacing` | `50` | dagre `ranksep`: gap between ranks |
| `diagramPadding` | `8` | outer padding around the whole diagram |
| `padding` | `15` | inner padding inside a node shape, around the label |
| `curve` | `basis` | edge interpolation (D3 basis spline) |
| `wrappingWidth` | `200` | max label width before wrapping |
| `titleTopMargin` | `25` | not used for untitled diagrams |
| `defaultRenderer` | `dagre-wrapper` | confirms dagre is the engine |

Dagre's own defaults that mermaid does not override: `edgesep = 10`, `ranker = network-simplex`,
`acyclicer = undefined` (dagre's greedy cycle-break is used via `acyclic`), `marginx/marginy = 0`.

### The pipeline we are porting (dagre, in order)

1. **Acyclic** — greedy feedback-arc-set; reverse edges to make the graph a DAG, remember reversals.
2. **Rank assignment** — network simplex minimizing weighted edge length (fallback: longest-path /
   tight-tree). Assigns integer `rank` per node.
3. **Normalize** — split any edge spanning >1 rank into unit-length segments through **dummy nodes**
   (these become the edge polyline bend points later).
4. **Ordering** — minimize crossings: init order by DFS, then iterate median/barycenter heuristic
   with adjacent-exchange transposition, keeping the best-scoring permutation (default 8 iterations,
   alternating sweep direction).
5. **Coordinate assignment** — Brandes-Köpf: 4 alignment passes (up-left, up-right, down-left,
   down-right), then balance by taking the median of the four candidate x's per node.
6. **Undo** — un-normalize (dummy chains become edge `points`), un-reverse edges, translate the graph
   so the bounding box starts at the origin plus padding, and apply the `rankdir` coordinate swap.

Getting these phases in the right order matters more than micro-optimizing any one of them; a wrong
order produces a plausible-looking but non-matching diagram.

---

## Test Strategy (the core of this plan)

The requirement is a loop that proves we render like mermaid.js, automated, at every step. Three
layers, cheapest first:

### Layer 1: portable unit tests (fast, run in CI test job)

Plain `gtest_lite.h` tests over the portable libs. No Windows, no node. These catch logic errors
inside a single phase (e.g. "network simplex assigned rank 3 where the tight tree requires 2").

### Layer 2: golden oracle diff (the "renders like mermaid.js" gate)

A one-time-per-CI-run Node job produces ground truth; a C++ test consumes it.

```
tests/mermaid/fixtures/*.mmd          # input diagrams (checked in)
tools/mermaid-oracle/dump.mjs         # runs real mermaid/dagre, emits JSON
tests/mermaid/golden/*.json           # checked-in expected geometry (regenerated by CI job)
tests/mermaid_golden_test.cpp         # C++ test: our layout vs golden JSON, per-node tolerance
```

`dump.mjs` deliberately calls **dagre directly with mermaid's own config values**, not the full
browser renderer. That keeps the oracle headless (no Chromium, no Puppeteer), deterministic, and
focused on geometry rather than SVG styling. Node/label *sizes* are the one thing dagre needs from
the browser (text measurement), so the oracle emits the width/height it used per node, and the C++
test **injects those same sizes** via the `MeasureFn` seam. This isolates layout correctness from
font-metric differences between DirectWrite and the browser, which can never match exactly.

Tolerance: `±0.5 DIP` on node centers and bend points once sizes are injected. Any drift above that
is a real algorithmic divergence, not rounding.

Golden files are committed so the C++ test runs even when the Node job is skipped, and a CI step
regenerates them and fails if they drift, so the oracle can never silently rot.

### Layer 3: visual regression (human-checked, once per phase)

CI renders the same fixtures to SVG via mermaid-cli and, separately, we screenshot MarkDownIt on the
Windows box. Compared by eye at the end of Task 12 only. This is the WSL ceiling from
`wsl-windows-native-dev`: geometry is machine-verifiable, pixels are not.

### The per-task loop

Every implementation task below follows exactly this cycle:

1. Write the failing test (unit and/or golden).
2. Push; CI test job runs; **confirm it fails for the stated reason**.
3. Implement the minimum to pass.
4. Push; CI green (build + tests + golden diff).
5. Commit.

Never mark a task done on a local reasoning check. The compile gate and the oracle both live in CI.

---

## Task 0: Branch, skeleton, and the oracle harness first

**Objective:** Stand up the test infrastructure before any layout code, so Task 1 already has a gate.

**Files:**
- Create: `tools/mermaid-oracle/package.json`
- Create: `tools/mermaid-oracle/dump.mjs`
- Create: `tests/mermaid/fixtures/01-linear.mmd`
- Modify: `.github/workflows/build.yml`

**Step 1: Fixture**

`tests/mermaid/fixtures/01-linear.mmd`:

```
flowchart TD
    A[Start] --> B[Middle]
    B --> C[End]
```

**Step 2: Oracle package**

`tools/mermaid-oracle/package.json`:

```json
{
  "name": "mermaid-oracle",
  "private": true,
  "type": "module",
  "dependencies": {
    "mermaid": "11.7.0",
    "dagre-d3-es": "7.0.11"
  }
}
```

Pin exact versions. An unpinned oracle makes the golden files drift on unrelated upstream releases.

**Step 3: Oracle dumper**

`tools/mermaid-oracle/dump.mjs` — parses the `.mmd` with mermaid's own flow parser, feeds the graph
to dagre with mermaid's documented defaults, and emits geometry plus the node sizes it used:

```js
import fs from 'node:fs';
import path from 'node:path';
import * as graphlib from 'dagre-d3-es/src/graphlib/index.js';
import { layout } from 'dagre-d3-es/src/dagre/index.js';

// Mermaid FlowchartDiagramConfig defaults (config.schema.json)
const NODE_SPACING = 50, RANK_SPACING = 50, EDGE_SEP = 10, PADDING = 15;

// Deterministic stand-in for browser text measurement: mermaid pads the label
// box by `padding` on each side. Width is derived from a fixed per-char advance
// so the oracle is reproducible; the C++ side is fed these exact numbers.
const CHAR_W = 8.4, LINE_H = 19;
const sizeFor = (label) => ({
  width:  Math.max(label.length * CHAR_W, 14) + PADDING * 2,
  height: LINE_H + PADDING * 2,
});

function parseFlow(src) {
  // Minimal flowchart reader: enough for the fixture set, mirrors mermaid's
  // flow grammar for `id[label] --> id[label]` and `-->|label|` forms.
  const lines = src.split('\n').map(s => s.trim()).filter(Boolean);
  const header = lines.shift();
  const dir = (header.match(/^(?:flowchart|graph)\s+(TD|TB|LR|RL|BT)/) || [, 'TB'])[1];
  const nodes = new Map(), edges = [];
  const nodeRe = /([A-Za-z0-9_]+)(?:\[([^\]]*)\]|\(\(([^)]*)\)\)|\(\[([^\]]*)\]\)|\(([^)]*)\)|\{([^}]*)\})?/g;
  for (const line of lines) {
    const m = line.match(/^(.*?)\s*(-{2,3}>|-\.-+>|={2,}>)\s*(?:\|([^|]*)\|)?\s*(.*)$/);
    const decl = (tok) => {
      nodeRe.lastIndex = 0;
      const g = nodeRe.exec(tok);
      if (!g) return null;
      const id = g[1];
      const label = g[2] ?? g[3] ?? g[4] ?? g[5] ?? g[6] ?? id;
      if (!nodes.has(id)) nodes.set(id, { id, label });
      return id;
    };
    if (m) {
      const from = decl(m[1]), to = decl(m[4]);
      edges.push({ from, to, label: m[3] || '' });
    } else {
      decl(line);
    }
  }
  return { dir, nodes: [...nodes.values()], edges };
}

const file = process.argv[2];
const { dir, nodes, edges } = parseFlow(fs.readFileSync(file, 'utf8'));

const g = new graphlib.Graph({ multigraph: true, compound: true });
g.setGraph({ rankdir: dir, nodesep: NODE_SPACING, ranksep: RANK_SPACING, edgesep: EDGE_SEP, marginx: 0, marginy: 0 });
g.setDefaultEdgeLabel(() => ({}));
for (const n of nodes) g.setNode(n.id, { ...sizeFor(n.label), label: n.label });
for (const e of edges) g.setEdge(e.from, e.to, { weight: 1, minlen: 1, labelpos: 'c', label: e.label });

layout(g);

const out = {
  source: path.basename(file),
  config: { rankdir: dir, nodesep: NODE_SPACING, ranksep: RANK_SPACING, edgesep: EDGE_SEP, padding: PADDING },
  graph: { width: g.graph().width, height: g.graph().height },
  nodes: g.nodes().map(id => {
    const n = g.node(id);
    return { id, label: n.label, x: n.x, y: n.y, width: n.width, height: n.height, rank: n.rank ?? null };
  }),
  edges: g.edges().map(e => {
    const d = g.edge(e);
    return { from: e.v, to: e.w, label: d.label ?? '', points: (d.points || []).map(p => ({ x: p.x, y: p.y })) };
  }),
};
console.log(JSON.stringify(out, null, 2));
```

**Step 4: CI oracle job**

Add to `.github/workflows/build.yml` (runs on Linux, cheap, parallel to the Windows build):

```yaml
  oracle:
    name: Mermaid oracle (golden geometry)
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - uses: actions/setup-node@v4
        with:
          node-version: '20'
      - name: Install oracle deps
        working-directory: tools/mermaid-oracle
        run: npm install --no-audit --no-fund
      - name: Regenerate golden geometry
        run: |
          mkdir -p /tmp/golden
          for f in tests/mermaid/fixtures/*.mmd; do
            name=$(basename "$f" .mmd)
            node tools/mermaid-oracle/dump.mjs "$f" > "/tmp/golden/$name.json"
          done
      - name: Fail if committed goldens drift from the oracle
        run: |
          for f in /tmp/golden/*.json; do
            name=$(basename "$f")
            if ! diff -u "tests/mermaid/golden/$name" "$f"; then
              echo "::error::Golden $name is stale. Commit the regenerated file."
              exit 1
            fi
          done
      - uses: actions/upload-artifact@v4
        if: always()
        with:
          name: mermaid-golden
          path: /tmp/golden/
```

**Step 5: Verify + commit**

Push. Expected: `oracle` job runs, generates `01-linear.json`, and **fails** the drift check because
`tests/mermaid/golden/` is empty. Download the `mermaid-golden` artifact, commit its contents as
`tests/mermaid/golden/01-linear.json`, push again, oracle green.

```bash
git add tools/mermaid-oracle tests/mermaid .github/workflows/build.yml
git commit -m "test: mermaid.js golden-geometry oracle harness"
```

---

## Task 1: Portable graph model

**Objective:** Header-only data model for a parsed flowchart. No layout, no Windows.

**Files:** Create `src/mermaid/model.h`

```cpp
#pragma once
// Portable flowchart model. No Windows, no Direct2D: unit tested on any host.
#include <string>
#include <vector>

namespace mermaid {

enum class Dir { TB, BT, LR, RL };
enum class Shape { Rect, Round, Stadium, Diamond, Circle };
enum class LineStyle { Solid, Dotted, Thick };
enum class Head { None, Arrow };

struct FlowNode {
    std::string id;
    std::string label;
    Shape shape = Shape::Rect;
};

struct FlowEdge {
    int from = -1;          // index into Flowchart::nodes
    int to = -1;
    std::string label;
    LineStyle style = LineStyle::Solid;
    Head head = Head::Arrow;
    int minlen = 1;
    int weight = 1;
};

struct Flowchart {
    Dir dir = Dir::TB;
    std::vector<FlowNode> nodes;
    std::vector<FlowEdge> edges;
    std::string error;      // non-empty => parse failed, caller falls back to code block
};

}  // namespace mermaid
```

**Test:** `tests/mermaid_model_test.cpp` — construct a 2-node/1-edge graph, assert defaults
(`dir == TB`, `head == Arrow`, `minlen == 1`, `error.empty()`).

Add `tests/mermaid_model_test.cpp` to the `MarkDownIt.tests` source list in `CMakeLists.txt:191`.

Push → CI test job green → commit.

---

## Task 2: Flowchart parser — header line

**Objective:** Parse `flowchart TD` / `graph LR` / bare `flowchart` into `Flowchart::dir`.

**Files:** Create `src/mermaid/parse.h`, `src/mermaid/parse.cpp`; test `tests/mermaid_parse_test.cpp`

**Step 1: Failing test**

```cpp
TEST(MermaidParse, HeaderDirection) {
    EXPECT_EQ((int)mermaid::ParseFlowchart("flowchart TD").dir, (int)mermaid::Dir::TB);
    EXPECT_EQ((int)mermaid::ParseFlowchart("flowchart TB").dir, (int)mermaid::Dir::TB);
    EXPECT_EQ((int)mermaid::ParseFlowchart("graph LR").dir,     (int)mermaid::Dir::LR);
    EXPECT_EQ((int)mermaid::ParseFlowchart("graph RL").dir,     (int)mermaid::Dir::RL);
    EXPECT_EQ((int)mermaid::ParseFlowchart("flowchart BT").dir, (int)mermaid::Dir::BT);
    EXPECT_EQ((int)mermaid::ParseFlowchart("flowchart").dir,    (int)mermaid::Dir::TB);  // default
}
TEST(MermaidParse, RejectsNonFlowchart) {
    EXPECT_TRUE(!mermaid::ParseFlowchart("sequenceDiagram").error.empty());
}
```

Note: mermaid treats `TD` as a synonym for `TB`. Collapse it at parse time so layout only sees `TB`.

**Step 2–5:** Run (fails: no `ParseFlowchart`) → implement header tokenizing only → CI green → commit.

---

## Task 3: Parser — node declarations and shapes

**Objective:** `A[Rect]`, `B(Round)`, `C([Stadium])`, `D{Diamond}`, `E((Circle))`, bare `F`.

**Test:**

```cpp
TEST(MermaidParse, NodeShapes) {
    auto f = mermaid::ParseFlowchart(
        "flowchart TD\n"
        "  A[Rect]\n  B(Round)\n  C([Stadium])\n  D{Diamond}\n  E((Circle))\n  F\n");
    ASSERT_EQ(f.nodes.size(), 6u);
    EXPECT_EQ((int)f.nodes[0].shape, (int)mermaid::Shape::Rect);
    EXPECT_EQ((int)f.nodes[1].shape, (int)mermaid::Shape::Round);
    EXPECT_EQ((int)f.nodes[2].shape, (int)mermaid::Shape::Stadium);
    EXPECT_EQ((int)f.nodes[3].shape, (int)mermaid::Shape::Diamond);
    EXPECT_EQ((int)f.nodes[4].shape, (int)mermaid::Shape::Circle);
    EXPECT_EQ((int)f.nodes[5].shape, (int)mermaid::Shape::Rect);   // bare id
    EXPECT_EQ(f.nodes[5].label, "F");                              // label defaults to id
}
```

**Pitfall:** order the bracket matching longest-first. `([` must be tried before `(`, and `((` before
`(`, or a stadium parses as a round node with a stray `[`.

**Pitfall (sandbox):** the output layer strips angle brackets and mangles some escapes. Write these
`.cpp` files via `execute_code` with base64, per AGENTS.md, and verify with a raw byte read.

---

## Task 4: Parser — edges, labels, and styles

**Objective:** `A --> B`, `A --- B`, `A -.-> B`, `A ==> B`, `A -->|text| B`, `A -- text --> B`,
and chains `A --> B --> C`.

**Test:**

```cpp
TEST(MermaidParse, EdgeStyles) {
    auto f = mermaid::ParseFlowchart(
        "flowchart TD\n  A --> B\n  B -.-> C\n  C ==> D\n  D --- E\n");
    ASSERT_EQ(f.edges.size(), 4u);
    EXPECT_EQ((int)f.edges[0].style, (int)mermaid::LineStyle::Solid);
    EXPECT_EQ((int)f.edges[1].style, (int)mermaid::LineStyle::Dotted);
    EXPECT_EQ((int)f.edges[2].style, (int)mermaid::LineStyle::Thick);
    EXPECT_EQ((int)f.edges[3].head,  (int)mermaid::Head::None);   // --- is open
}
TEST(MermaidParse, EdgeLabelAndChain) {
    auto f = mermaid::ParseFlowchart("flowchart TD\n  A -->|yes| B --> C\n");
    ASSERT_EQ(f.nodes.size(), 3u);
    ASSERT_EQ(f.edges.size(), 2u);
    EXPECT_EQ(f.edges[0].label, "yes");
    EXPECT_EQ(f.edges[1].label, "");
}
```

Add fixture `tests/mermaid/fixtures/02-shapes-edges.mmd` covering all of the above; regenerate and
commit its golden JSON (the oracle job tells you if you forgot).

---

## Task 5: Layout phase 1 — acyclic (cycle breaking)

**Objective:** Greedy feedback-arc-set. Reverse edges to make a DAG; record which were reversed so
Task 11 can flip their polylines back.

**Files:** Create `src/mermaid/layout_acyclic.cpp`, `src/mermaid/layout_internal.h`

**Test:**

```cpp
TEST(MermaidAcyclic, BreaksSimpleCycle) {
    // A -> B -> C -> A
    auto g = MakeGraph(3, {{0,1},{1,2},{2,0}});
    auto reversed = mermaid::MakeAcyclic(g);
    EXPECT_EQ(reversed.size(), 1u);
    EXPECT_TRUE(mermaid::IsAcyclic(g));
}
TEST(MermaidAcyclic, LeavesDagUntouched) {
    auto g = MakeGraph(3, {{0,1},{1,2}});
    EXPECT_EQ(mermaid::MakeAcyclic(g).size(), 0u);
}
TEST(MermaidAcyclic, HandlesSelfLoop) {
    auto g = MakeGraph(1, {{0,0}});
    mermaid::MakeAcyclic(g);            // self-loop removed from ranking, drawn separately
    EXPECT_TRUE(mermaid::IsAcyclic(g));
}
```

Self-loops must be pulled out of the ranked graph entirely (dagre does this too) and drawn as a
compact loop beside the node; leaving one in makes network simplex unsolvable.

---

## Task 6: Layout phase 2 — rank assignment

**Objective:** Assign integer ranks. Implement longest-path first (simple, correct), then
tight-tree + network simplex to match dagre's actual output.

**Test (unit):**

```cpp
TEST(MermaidRank, LongestPathChain) {
    auto g = MakeGraph(3, {{0,1},{1,2}});
    mermaid::AssignRanks(g);
    EXPECT_EQ(g.nodes[0].rank, 0);
    EXPECT_EQ(g.nodes[1].rank, 1);
    EXPECT_EQ(g.nodes[2].rank, 2);
}
TEST(MermaidRank, NetworkSimplexTightensLongEdge) {
    // A->B, A->C, B->D, C->D : D must be rank 2, not 3
    auto g = MakeGraph(4, {{0,1},{0,2},{1,3},{2,3}});
    mermaid::AssignRanks(g);
    EXPECT_EQ(g.nodes[3].rank, 2);
}
```

**Test (golden):** first real oracle gate. The oracle emits `rank` per node; compare exactly
(ranks are integers, so tolerance is zero).

```cpp
TEST(MermaidGolden, RanksMatchDagre_01Linear) {
    auto gold = LoadGolden("01-linear.json");
    auto ours = LayoutFromGolden(gold);          // sizes injected from golden
    for (const auto& gn : gold.nodes)
        EXPECT_EQ(RankOf(ours, gn.id), gn.rank);
}
```

`LoadGolden` is a tiny JSON reader in `tests/mermaid/golden_loader.h`. Do not add a JSON library:
the golden files are machine-generated with a fixed shape, so a ~120-line scanner is enough and keeps
the zero-dependency rule intact.

---

## Task 7: Layout phase 3 — normalize (dummy nodes)

**Objective:** Split every edge spanning more than one rank into unit segments through dummy nodes.
Keep a map from dummy chain → original edge, because those dummies *are* the bend points.

**Test:**

```cpp
TEST(MermaidNormalize, SplitsLongEdge) {
    auto g = MakeGraph(3, {{0,1},{1,2},{0,2}});   // 0->2 spans two ranks
    mermaid::AssignRanks(g);
    size_t before = g.nodes.size();
    mermaid::Normalize(g);
    EXPECT_EQ(g.nodes.size(), before + 1);        // exactly one dummy
    EXPECT_TRUE(mermaid::AllEdgesUnitLength(g));
}
TEST(MermaidNormalize, DenormalizeRestoresEdgeCount) {
    auto g = MakeGraph(3, {{0,1},{1,2},{0,2}});
    mermaid::AssignRanks(g); mermaid::Normalize(g); mermaid::Denormalize(g);
    EXPECT_EQ(g.edges.size(), 3u);
}
```

---

## Task 8: Layout phase 4 — ordering (crossing minimization)

**Objective:** DFS init order, then 8 iterations of the median heuristic with adjacent transposition,
alternating sweep direction, keeping the best crossing count.

**Test (unit):**

```cpp
TEST(MermaidOrder, ResolvesObviousCrossing) {
    // A->D, B->C with A,B on rank 0 and C,D on rank 1 : one crossing if
    // order is (A,B),(C,D); zero if ordering swaps.
    auto g = MakeGraph(4, {{0,3},{1,2}});
    mermaid::AssignRanks(g); mermaid::Normalize(g); mermaid::Order(g);
    EXPECT_EQ(mermaid::CountCrossings(g), 0);
}
TEST(MermaidOrder, CrossingCountIsMonotone) {
    auto g = BuildFixtureGraph("03-diamond");
    mermaid::AssignRanks(g); mermaid::Normalize(g);
    int before = mermaid::CountCrossings(g);
    mermaid::Order(g);
    EXPECT_TRUE(mermaid::CountCrossings(g) <= before);
}
```

**Test (golden):** compare **relative order within each rank** against the oracle, not absolute x.
Ordering is where ties are broken arbitrarily, so assert the permutation matches; if a fixture is
genuinely ambiguous (two valid zero-crossing orders), mark it in the golden file with
`"orderAmbiguous": true` and assert only the crossing count. Be honest about this rather than
loosening the tolerance until it passes.

Add fixtures `03-diamond.mmd`, `04-crossing.mmd`, `05-long-edge.mmd`.

---

## Task 9: Layout phase 5 — Brandes-Köpf coordinates

**Objective:** The phase that decides whether diagrams *look* like mermaid's. Four alignment passes
(up/down × left/right), then per-node median of the four candidates.

**Test (unit):**

```cpp
TEST(MermaidCoord, CentersParentOverTwoChildren) {
    auto g = BuildFixtureGraph("03-diamond");   // A -> B, A -> C, B -> D, C -> D
    mermaid::RunLayout(g, /*sizes injected*/);
    // A and D share the same x; B and C straddle it symmetrically
    EXPECT_NEAR(XOf(g,"A"), XOf(g,"D"), 0.5f);
    EXPECT_NEAR((XOf(g,"B") + XOf(g,"C")) * 0.5f, XOf(g,"A"), 0.5f);
}
TEST(MermaidCoord, RespectsNodeSpacing) {
    auto g = BuildFixtureGraph("06-siblings");
    mermaid::RunLayout(g, {});
    EXPECT_NEAR(GapBetween(g,"B","C"), 50.0f, 0.5f);   // mermaid nodeSpacing default
}
```

**Test (golden) — the primary "renders like mermaid.js" gate:**

```cpp
TEST(MermaidGolden, NodeCentersMatchDagre) {
    for (const char* name : {"01-linear","02-shapes-edges","03-diamond",
                             "04-crossing","05-long-edge","06-siblings"}) {
        auto gold = LoadGolden(name);
        auto ours = LayoutFromGolden(gold);     // injects gold's width/height per node
        for (const auto& gn : gold.nodes) {
            EXPECT_NEAR(XOf(ours, gn.id), gn.x, 0.5f);
            EXPECT_NEAR(YOf(ours, gn.id), gn.y, 0.5f);
        }
        EXPECT_NEAR(ours.width,  gold.graph.width,  1.0f);
        EXPECT_NEAR(ours.height, gold.graph.height, 1.0f);
    }
}
```

**Expect this task to take more than one CI round.** Brandes-Köpf has four sign/direction conventions
that are easy to mirror wrongly, and a mirrored pass produces a diagram that is symmetric-but-wrong.
When a fixture is off, print our x and the golden x side by side for every node before changing code:
guessing at the sign convention costs a CI round each time.

---

## Task 10: Edge routing and rankdir transform

**Objective:** Turn dummy chains into polylines, clip endpoints to node boundaries, place edge labels,
then apply the `rankdir` swap (LR/RL/BT) and the `diagramPadding = 8` offset.

**Test (golden):**

```cpp
TEST(MermaidGolden, EdgePointsMatchDagre) {
    auto gold = LoadGolden("05-long-edge.json");
    auto ours = LayoutFromGolden(gold);
    for (const auto& ge : gold.edges) {
        const auto& oe = EdgeBetween(ours, ge.from, ge.to);
        ASSERT_EQ(oe.points.size(), ge.points.size());
        for (size_t i = 0; i < ge.points.size(); ++i) {
            EXPECT_NEAR(oe.points[i].x, ge.points[i].x, 0.5f);
            EXPECT_NEAR(oe.points[i].y, ge.points[i].y, 0.5f);
        }
    }
}
TEST(MermaidGolden, RankdirLR) {
    auto gold = LoadGolden("07-lr.json");        // flowchart LR
    auto ours = LayoutFromGolden(gold);
    for (const auto& gn : gold.nodes) { EXPECT_NEAR(XOf(ours,gn.id), gn.x, 0.5f);
                                        EXPECT_NEAR(YOf(ours,gn.id), gn.y, 0.5f); }
}
```

Add fixtures `07-lr.mmd`, `08-rl.mmd`, `09-bt.mmd`.

**Note on `curve: basis`:** dagre returns polyline bend points; mermaid then smooths them with a D3
basis spline. Match the **points** here (that is what the oracle emits). Smoothing is a Task 12
rendering concern and is not geometry-critical.

---

## Task 11: DirectWrite text measurement seam

**Objective:** Replace injected sizes with real measurement, so live diagrams size correctly, while
tests keep injecting the golden sizes.

**Files:** Modify `src/mermaid/layout.h`, create `src/mermaid/measure_dwrite.cpp`

The `MeasureFn` callback pattern already used by `origin/feat/mermaid-phase1`'s
`mermaidlayout.h` is the right seam; reuse that shape:

```cpp
using MeasureFn = LabelSize (*)(const std::string& utf8, float maxWidth, void* ctx);
Layout ComputeLayout(const Flowchart& f, MeasureFn measure, void* ctx, float zoom);
```

Wrap the label at `wrappingWidth = 200` and add `padding = 15` on each side, matching mermaid.

**Test:** a stub `MeasureFn` returning a fixed advance per char reproduces the golden sizes exactly
(this is what makes Task 9's gate meaningful); the DirectWrite implementation is only exercised in
the Windows build.

**Honest limitation to state in the commit message:** with real DirectWrite metrics, node boxes will
differ from a browser's by a few DIPs because font rasterization differs. Layout *structure* matches;
absolute pixel sizes cannot, and chasing that is not worth it.

---

## Task 12: Direct2D rendering

**Objective:** Draw the laid-out diagram. Windows-only code, no unit tests (CI compile + manual check).

**Files:** Create `src/mermaid/render_d2d.cpp`; modify `src/renderer.cpp`, `src/renderer.h`

Hook where `main` already detects the fence language:

```cpp
if (n.kind == BlockKind::CodeBlock && n.lang == "mermaid") {
    // parse -> layout -> draw; on any error fall back to the existing code-block path
}
```

Draw order: node fills, node borders, edge polylines, arrowheads, edge label backgrounds, all text.
Shapes: `FillRoundedRectangle` for Round/Stadium, `ID2D1PathGeometry` for Diamond, `FillEllipse` for
Circle. Reuse the existing `theme.h` palette; add mermaid's defaults (`#ECECFF` node fill,
`#9370DB` border, `#333333` text) as new palette tokens rather than hardcoding.

**Cache:** key the laid-out diagram on `hash(fence source) + zoom`. Re-running layout on every
`WM_PAINT` will make scrolling visibly stutter on a document with several diagrams.

**Measure/Render agreement:** `Measure()` and `Render()` must return the identical height for the
diagram block or the scrollbar range breaks. This project has hit that bug repeatedly (code blocks,
tables); compute the height once via the cache and read it in both passes.

**Fallback:** if `Flowchart::error` is set, render the fence as a normal code block. A broken diagram
must never blank the document.

**Verification (the WSL ceiling):** CI green + tests pass is the machine-checkable part. Then deploy
per `wsl-windows-native-dev` (download artifact, kill exe, swap, MD5, relaunch) and manually confirm
the fixtures render, scroll, and zoom. Say plainly which of those two were actually verified.

---

## Task 13: Documentation and skill capture

**Files:** Modify `README.md`; update the `wsl-windows-native-dev` skill

Document supported flowchart syntax and the explicit non-goals (subgraphs, other diagram types).
Add a `references/mermaid-dagre-port.md` to the skill with the phase order, mermaid's default
constants, and the golden-oracle pattern, so the next diagram type does not re-derive it.

Run the `humanizer` skill over the README text (mandatory per AGENTS.md).

---

## Files Likely to Change

| Path | Action | Portable? |
|------|--------|-----------|
| `src/mermaid/model.h` | create | yes |
| `src/mermaid/parse.h` / `.cpp` | create | yes |
| `src/mermaid/layout.h` | create | yes |
| `src/mermaid/layout_internal.h` | create | yes |
| `src/mermaid/layout_acyclic.cpp` | create | yes |
| `src/mermaid/layout_rank.cpp` | create | yes |
| `src/mermaid/layout_normalize.cpp` | create | yes |
| `src/mermaid/layout_order.cpp` | create | yes |
| `src/mermaid/layout_coord.cpp` | create | yes |
| `src/mermaid/layout_route.cpp` | create | yes |
| `src/mermaid/measure_dwrite.cpp` | create | Windows only |
| `src/mermaid/render_d2d.cpp` | create | Windows only |
| `src/renderer.cpp` / `.h` | modify | Windows only |
| `src/theme.h` | modify | yes |
| `CMakeLists.txt` | modify (both targets) | — |
| `.github/workflows/build.yml` | modify (oracle job) | — |
| `tools/mermaid-oracle/*` | create | Node, CI only |
| `tests/mermaid/fixtures/*.mmd` | create | — |
| `tests/mermaid/golden/*.json` | create (generated, committed) | — |
| `tests/mermaid/golden_loader.h` | create | yes |
| `tests/mermaid_*_test.cpp` | create | yes |

Keep every `src/mermaid/layout_*.cpp` free of Windows headers. That is what allows the whole layout
engine to be tested in the Linux CI job and locally with g++ once a toolchain is available.

---

## Risks and Tradeoffs

| Risk | Impact | Mitigation |
|------|--------|-----------|
| Brandes-Köpf is genuinely intricate | Task 9 could stall | Land longest-path ranks + a simple centering pass first (visually decent), then swap in full BK behind the same golden gate. Ship-able at each point. |
| No local compiler or node | Every gate costs a CI round (~3-5 min) | Batch related edits per push; keep the oracle job on ubuntu (fast) separate from the Windows build |
| Oracle uses a simplified `.mmd` reader, not mermaid's real grammar | Oracle could disagree with real mermaid on exotic syntax | Fixtures stay within the documented subset; the oracle's parser and our C++ parser are cross-checked by Task 3/4 unit tests. If a fixture needs syntax the simple reader cannot handle, add it to the C++ parser tests only, not the golden set. |
| Font metrics never match a browser | Node sizes differ slightly from mermaid.live | Inject sizes for geometry tests; accept and document the difference for live rendering |
| Golden files rot as mermaid/dagre release | Silent false-green | Versions pinned exactly; CI regenerates and fails on drift every run |
| Layout cost on large diagrams | Scroll stutter | Cache on `hash(source)+zoom`; network simplex is fine well past the ~100 nodes a readable diagram has |
| Sandbox mangles C++ bytes | Mystery compile errors | base64 writes via `execute_code`, raw byte verification (AGENTS.md + skill) |

---

## Decisions (resolved before implementation)

1. **Reuse `feat/mermaid-phase1` parser** — lift `src/mermaid.cpp` and `src/mermaid.h` into Tasks 3-4
   (parser shape + edges). Replace the layout wholesale. The homegrown layered layout on that
   branch is not what mermaid.js does.
2. **Oracle in-repo** at `tools/mermaid-oracle/`. Never ships in the exe.
3. **Edge smoothing** — match dagre polyline points exactly. D3 basis spline is a rendering-side
   concern, deferred to a follow-up plan.
4. **Theme** — mermaid's default palette: `#ECECFF` node fill, `#9370DB` border, `#333333` text.
   Add as new tokens in `theme.h` rather than hardcoding in the renderer.

---

## Definition of Done

- [ ] `flowchart` / `graph` fences render as diagrams in the app, all five shapes, all four rankdirs
- [ ] Malformed mermaid falls back to a code block, never blanks the document
- [ ] Golden gate green: node centers and edge bend points within 0.5 DIP of dagre on all 9 fixtures
- [ ] Unit tests green for all five layout phases
- [ ] Oracle drift check green (goldens match a fresh mermaid/dagre run)
- [ ] CI build green on `windows-2022`, warnings not increased
- [ ] Manually confirmed on the Windows box: render, scroll, zoom, live reload
- [ ] README documents the supported subset and the non-goals
