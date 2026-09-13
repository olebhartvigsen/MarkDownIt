import * as graphlib from 'dagre-d3-es/src/graphlib/index.js';
import { layout } from 'dagre-d3-es/src/dagre/index.js';
import { readFileSync } from 'node:fs';

const name = 'cls5';
const golden = JSON.parse(readFileSync(`/workspace/MarkDownIt/tests/mermaid/golden/${name}.json`, 'utf8'));
const g = new graphlib.Graph({ multigraph: true, compound: true });
g.setGraph({ rankdir: 'TB', nodesep: 50, ranksep: 50, edgesep: 20, marginx: 8, marginy: 8 });
g.setDefaultEdgeLabel(() => ({ minlen: 1, weight: 1, width: 0, height: 0, labeloffset: 10, labelpos: 'c' }));
for (const n of golden.nodes) g.setNode(n.id, { width: n.w, height: n.h });
const labels = { 'Migration|Repository': ['schema', 48], 'Migration|SqlRepository': ['upgrades', 64] };
for (const e of golden.edges) {
  const attrs = { minlen: 1, weight: 1, width: 0, height: 0, labeloffset: 10, labelpos: 'c' };
  const L = labels[`${e.from}|${e.to}`];
  if (L) { attrs.width = L[1]; attrs.height = 20; }
  g.setEdge(e.from, e.to, attrs);
}
layout(g);
for (const v of g.nodes()) {
  const n = g.node(v);
  console.log(v, 'rank=' + n.rank, 'h=' + n.height, 'y=' + (n.y ?? '?').toFixed ? (n.y ?? 0).toFixed(1) : n.y);
}
