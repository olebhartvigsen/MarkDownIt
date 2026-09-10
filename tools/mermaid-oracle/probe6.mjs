import * as graphlib from 'dagre-d3-es/src/graphlib/index.js';
import { layout } from 'dagre-d3-es/src/dagre/index.js';
import { readFileSync } from 'node:fs';

for (const name of ['cls1','cls2','cls3','cls4','cls5']) {
  const golden = JSON.parse(readFileSync(`/workspace/MarkDownIt/tests/mermaid/golden/${name}.json`, 'utf8'));
  const c = golden.canvas;
  const minx = Math.min(...golden.nodes.map(n => n.cx - n.w/2));
  const maxx = Math.max(...golden.nodes.map(n => n.cx + n.w/2));
  const miny = Math.min(...golden.nodes.map(n => n.cy - n.h/2));
  const maxy = Math.max(...golden.nodes.map(n => n.cy + n.h/2));
  const nw = maxx - minx, nh = maxy - miny;
  console.log(name,
    'nodeW', nw, 'canvasW', c.vbwidth, 'diff', c.vbwidth - nw,
    '| nodeH', nh, 'canvasH', c.vbheight, 'diff', c.vbheight - nh,
    '| startx', c.startx, 'minx', minx, 'starty', c.starty, 'miny', miny);
}
