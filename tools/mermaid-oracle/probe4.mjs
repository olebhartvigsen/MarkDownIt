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
  for (const e of golden.edges) g.setEdge(e.from, e.to, {});
  layout(g);
  const minx = Math.min(...golden.nodes.map(n => n.cx - n.w/2));
  const miny = Math.min(...golden.nodes.map(n => n.cy - n.h/2));
  // edge label positions and terminals contribute to translateGraph extremes.
  // dagre width = maxX - minX + marginx where maxX includes edge label x+w/2.
  console.log('==', name);
  console.log(' node minx', minx, 'miny', miny);
  for (const e of golden.edges) {
    const pts = e.points;
    console.log('  edge', e.from, '->', e.to, 'first', pts[0].x, pts[0].y, 'last', pts[pts.length-1].x, pts[pts.length-1].y);
  }
  const lbl = (golden.edge_labels ?? []);
  for (const l of lbl) console.log('  label', l.kind, l.text, l.x, l.y, 'w', l.w);
  // what dagre reports vs what golden implies
  console.log('  golden canvas w', golden.canvas.vbwidth, 'h', golden.canvas.vbheight, 'startx', golden.canvas.startx, 'starty', golden.canvas.starty);
}
