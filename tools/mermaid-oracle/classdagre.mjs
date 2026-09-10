
import * as graphlib from 'dagre-d3-es/src/graphlib/index.js';
import { layout } from 'dagre-d3-es/src/dagre/index.js';
import { readFileSync } from 'node:fs';
for (const name of ['cls3','cls4','cls5']) {
  const golden = JSON.parse(readFileSync(`/workspace/MarkDownIt/tests/mermaid/golden/${name}.json`, 'utf8'));
  const g = new graphlib.Graph({ multigraph: true, compound: true });
  g.setGraph({ rankdir: 'TB', nodesep: 50, ranksep: 50, edgesep: 20, marginx: 8, marginy: 8 });
  g.setDefaultEdgeLabel(() => ({ minlen: 1, weight: 1, width: 0, height: 0, labeloffset: 10, labelpos: 'r' }));
  for (const n of golden.nodes) g.setNode(n.id, { width: n.w, height: n.h, label: n.title });
  for (const e of golden.edges) g.setEdge(e.from, e.to, {});
  layout(g);
  for (const n of golden.nodes) {
    const gn = g.node(n.id);
    console.log(name, n.id, 'dagre', Math.round(gn.x*10)/10, Math.round(gn.y*10)/10, 'golden', n.cx, n.cy);
  }
}
