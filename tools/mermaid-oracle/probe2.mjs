import * as graphlib from 'dagre-d3-es/src/graphlib/index.js';
import { layout } from 'dagre-d3-es/src/dagre/index.js';
import { readFileSync } from 'node:fs';

for (const name of ['cls1','cls2','cls3','cls4','cls5']) {
  const golden = JSON.parse(readFileSync(`/workspace/MarkDownIt/tests/mermaid/golden/${name}.json`, 'utf8'));
  const dir = name === 'cls2' ? 'LR' : 'TB';
  const g = new graphlib.Graph({ multigraph: true, compound: true });
  g.setGraph({ rankdir: dir, nodesep: 50, ranksep: 50, edgesep: 20, marginx: 8, marginy: 8 });
  g.setDefaultEdgeLabel(() => ({ minlen: 1, weight: 1, width: 0, height: 0, labeloffset: 10, labelpos: 'r' }));
  for (const n of golden.nodes) g.setNode(n.id, { width: n.w, height: n.h });
  // mermaid class edges: labelpos c, label text -> width/height via getBoundingClientRect shim (8/char, 20)
  for (const e of golden.edges) {
    const attrs = { minlen: 1, weight: 1, width: 0, height: 0, labeloffset: 10, labelpos: 'c' };
    const lab = (golden.edge_labels ?? []).find(l => l.kind === 'label' && l.text);
    // match label to edge by order is fragile; use fixture labels map instead below
    g.setEdge(e.from, e.to, attrs);
  }
  // attach labels per fixture knowledge
  const labelMap = {
    cls3: { 'Car|Engine': ['owns', 32], 'Car|Wheel': ['has', 24], 'Driver|Wheel': ['drives', 48] },
    cls4: { 'App|Logger': ['uses', 32] },
    cls5: { 'Migration|Repository': ['schema', 48] },
  };
  const map = labelMap[name];
  if (map) {
    for (const e of golden.edges) {
      const key = e.from + '|' + e.to;
      if (map[key]) {
        const [, lw] = map[key];
        g.edge(e.from, e.to).width = lw;
        g.edge(e.from, e.to).height = 20;
      }
    }
  }
  layout(g);
  let maxerr = 0;
  for (const n of golden.nodes) {
    const gn = g.node(n.id);
    const err = Math.max(Math.abs(gn.x - n.cx), Math.abs(gn.y - n.cy));
    maxerr = Math.max(maxerr, err);
    if (err > 0.5) console.log('  DIFF', name, n.id, 'dagre', gn.x.toFixed(1), gn.y.toFixed(1), 'golden', n.cx, n.cy);
  }
  const gw = g.graph().width, gh = g.graph().height;
  const c = golden.canvas;
  const cerr = Math.max(Math.abs(gw - c.vbwidth), Math.abs(gh - c.vbheight), Math.abs(g.graph().x ?? 0 - c.startx));
  console.log(name, dir, 'maxNodeErr=', maxerr.toFixed(3), 'canvas dagre', gw.toFixed(1), gh.toFixed(1), 'golden', c.vbwidth, c.vbheight, 'startx dagre', (g.graph().x ?? '?'), 'golden', c.startx);
}
