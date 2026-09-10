// Hypothesis: final svg = dagre translateGraph output, then viewBox set by
// setupViewPortForSVG from the svg bbox (getBBox of the whole svg), scaled 1,
// with graph^2-style viewBox fitting: start = -(bbox/2)? Check numbers.
import * as graphlib from 'dagre-d3-es/src/graphlib/index.js';
import { layout } from 'dagre-d3-es/src/dagre/index.js';
import { readFileSync } from 'node:fs';

for (const name of ['cls1','cls2','cls3','cls4','cls5']) {
  const golden = JSON.parse(readFileSync(`/workspace/MarkDownIt/tests/mermaid/golden/${name}.json`, 'utf8'));
  const c = golden.canvas;
  // node bbox in golden coords
  const minx = Math.min(...golden.nodes.map(n => n.cx - n.w/2));
  const maxx = Math.max(...golden.nodes.map(n => n.cx + n.w/2));
  const miny = Math.min(...golden.nodes.map(n => n.cy - n.h/2));
  const maxy = Math.max(...golden.nodes.map(n => n.cy + n.h/2));
  // edge endpoints included
  for (const e of golden.edges) for (const p of e.points) {
    // skip node-intersection points
  }
  console.log(name, 'node bbox x', minx, maxx, 'y', miny, maxy,
    'canvas w', c.vbwidth, 'startx', c.startx, '=> maxx-startx =', maxx - c.startx,
    'startx + w =', c.startx + c.vbwidth, 'vs maxx', maxx);
}
