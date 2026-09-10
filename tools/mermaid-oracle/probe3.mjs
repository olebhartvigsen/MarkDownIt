import * as graphlib from 'dagre-d3-es/src/graphlib/index.js';
import { layout } from 'dagre-d3-es/src/dagre/index.js';
import { readFileSync } from 'node:fs';

// label width per edge from the real mermaid run: '' edges get width=0 height=20
// (the shim gives empty label 0x20; width 0 falsy -> no proxy)
for (const name of ['cls1','cls2','cls3','cls4','cls5']) {
  const golden = JSON.parse(readFileSync(`/workspace/MarkDownIt/tests/mermaid/golden/${name}.json`, 'utf8'));
  const dir = name === 'cls2' ? 'LR' : 'TB';
  const g = new graphlib.Graph({ multigraph: true, compound: true });
  g.setGraph({ rankdir: dir, nodesep: 50, ranksep: 50, edgesep: 20, marginx: 8, marginy: 8 });
  g.setDefaultEdgeLabel(() => ({ minlen: 1, weight: 1, width: 0, height: 0, labeloffset: 10, labelpos: 'r' }));
  for (const n of golden.nodes) g.setNode(n.id, { width: n.w, height: n.h });
  const labelMap = {
    cls3: { 'Car|Engine': 32, 'Car|Wheel': 24, 'Driver|Wheel': 48 },
    cls4: { 'App|Logger': 32 },
    cls5: { 'Migration|Repository': 48, 'Migration|SqlRepository': 64 },
  };
  const map = labelMap[name] ?? {};
  for (const e of golden.edges) {
    const attrs = { minlen: 1, weight: 1, width: map[e.from+'|'+e.to] ?? 0, height: 20, labeloffset: 10, labelpos: 'c' };
    g.setEdge(e.from, e.to, attrs);
  }
  layout(g);
  let maxerr = 0;
  for (const n of golden.nodes) {
    const gn = g.node(n.id);
    const err = Math.max(Math.abs(gn.x - n.cx), Math.abs(gn.y - n.cy));
    maxerr = Math.max(maxerr, err);
    if (err > 0.5) console.log('  DIFF', name, n.id, 'dagre', gn.x.toFixed(2), gn.y.toFixed(2), 'golden', n.cx, n.cy);
  }
  console.log(name, dir, 'maxNodeErr=', maxerr.toFixed(3));
}
