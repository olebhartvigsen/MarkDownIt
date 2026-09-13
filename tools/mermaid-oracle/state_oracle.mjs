// State-diagram oracle: render one fixture headless via the dom-shim and dump
// the geometric facts the native renderer must reproduce, as JSON.
// Mirrors class_oracle.mjs: nodes (circles/rects with centers + sizes),
// edge polylines (d string), edge labels, and the canvas.
//
// NOTE: requires the shim NaN-filter patch in chunk-F7MYA6JM.mjs
// (calcLabelPosition ignores NaN points); without it the state renderer
// crashes on edges with labels because the zero-width state nodes make
// dagre's border intersections NaN. The patch lives in node_modules
// (gitignored); restore it after any mermaid reinstall.
//
// The state renderer (mermaid 11.x dagre wrapper) emits:
//   - g.node groups: state-start circle (r=7), statediagram-state rects
//     (basic label-container), stateEnd circle, choice diamonds (path),
//     fork/join rects, notes
//   - g.edgePaths path d (basis spline, absolute), marker-end barb
//   - g.edgeLabels g.edgeLabel transform = label center; foreignObject w/h
//   - svg viewBox handles canvas; width/height attrs are shim-default 84/28
//     but the viewBox is authoritative for the real render
import { readFileSync, writeFileSync } from 'node:fs';
import { renderDiagram } from './oracle_util.mjs';

const srcPath = process.argv[2];
const outPath = process.argv[3];
if (!srcPath || !outPath) {
  console.error('usage: node state_oracle.mjs <fixture.mmd> <out.json>');
  process.exit(2);
}

const src = readFileSync(srcPath, 'utf8');
const { svg } = await renderDiagram('state', src, {});

const doc = new DOMParser().parseFromString(svg, 'image/svg+xml');
const svgEl = doc.querySelector('svg');
const viewBox = (svgEl.getAttribute('viewBox') ?? '').split(/\s+/).map(Number);
const numAttr = (name) => {
  const v = Number(svgEl.getAttribute(name));
  return Number.isFinite(v) ? v : 0;
};
const out = {
  canvas: {
    width: numAttr('width') || viewBox[2],
    height: numAttr('height') || viewBox[3],
    startx: viewBox[0], starty: viewBox[1],
    vbwidth: viewBox[2], vbheight: viewBox[3],
  },
  nodes: [],
  edges: [],
  edge_labels: [],
};

// ---- nodes ---------------------------------------------------------------
// g.node transform = dagre center. Children:
//   circle.state-start r=7            -> kind "start"
//   circle.state-end (r=7, cy offset) -> kind "end"
//   rect.basic label-container        -> kind "state" (w/h from rect)
//   path[state-choice]                -> kind "choice" (bbox from path d)
//   rect[fork-join]                   -> kind "fork"/"join"
//   rect[note]                        -> kind "note"
for (const nodeEl of doc.querySelectorAll('g.node')) {
  const rawId = nodeEl.getAttribute('id') ?? '';
  const m = rawId.match(/^state-(.+?)-\d+$/);
  const id = m ? m[1] : rawId;
  const tr = (nodeEl.getAttribute('transform') ?? '').match(
    /translate\(\s*([-\d.eE]+)[, ]+([-\d.eE]+)\s*\)/);
  const node = {
    id,
    kind: 'state',
    cx: tr ? Number(tr[1]) : 0,
    cy: tr ? Number(tr[2]) : 0,
    w: 0, h: 0,
    r: 0,
    text: '',
  };
  const startCirc = nodeEl.querySelector('circle.state-start');
  if (startCirc) {
    node.kind = 'start';
    node.r = Number(startCirc.getAttribute('r') || 7);
    node.w = node.r * 2;
    node.h = node.r * 2;
  } else if (id === 'root_end' || nodeEl.querySelector('circle.state-end')) {
    node.kind = 'end';
    node.r = 7;
    const p = nodeEl.querySelector('path');
    if (p) {
      const nums = (p.getAttribute('d') ?? '').match(/-?[\d.]+/g) ?? [];
      let minx = Infinity, miny = Infinity, maxx = -Infinity, maxy = -Infinity;
      for (let i = 0; i + 1 < nums.length; i += 2) {
        const x = Number(nums[i]), y = Number(nums[i + 1]);
        if (x < minx) minx = x; if (x > maxx) maxx = x;
        if (y < miny) miny = y; if (y > maxy) maxy = y;
      }
      if (minx !== Infinity) { node.w = maxx - minx; node.h = maxy - miny; }
    }
    if (node.w === 0) { node.w = 14; node.h = 14; }
  } else {
    const rect = nodeEl.querySelector('rect.basic.label-container');
    if (rect) {
      node.w = Number(rect.getAttribute('width') || 0);
      node.h = Number(rect.getAttribute('height') || 0);
    } else {
      const p = nodeEl.querySelector('path');
      if (p) {
        const d = p.getAttribute('d') ?? '';
        const cls = p.getAttribute('class') ?? '';
        const nums = (d.match(/-?[\d.]+/g) ?? []).map(Number);
        let minx = Infinity, miny = Infinity, maxx = -Infinity, maxy = -Infinity;
        for (let k = 0; k + 1 < nums.length; k += 2) {
          minx = Math.min(minx, nums[k]); maxx = Math.max(maxx, nums[k]);
          miny = Math.min(miny, nums[k + 1]); maxy = Math.max(maxy, nums[k + 1]);
        }
        if (minx !== Infinity) {
          node.kind = cls.includes('state-choice') ? 'choice' : 'fork';
          node.w = maxx - minx;
          node.h = maxy - miny;
        }
      }
    }
  }
  node.text = (nodeEl.textContent ?? '').trim().slice(0, 80);
  out.nodes.push(node);
}

// ---- edges ---------------------------------------------------------------
const decodeD = (d) => {
  const pts = [];
  const re = /(M|L|C)\s*([^MLCZz]+)/g;
  let m;
  while ((m = re.exec(d)) !== null) {
    const kind = m[1];
    const nums = (m[2].match(/-?[\d.]+/g) ?? []).map(Number);
    if (kind === 'C') {
      if (nums.length >= 6) pts.push({ x: nums[4], y: nums[5] });
    } else {
      for (let i = 0; i + 1 < nums.length; i += 2) pts.push({ x: nums[i], y: nums[i + 1] });
    }
  }
  return pts;
};

for (const p of doc.querySelectorAll('g.edgePaths path')) {
  const id = p.getAttribute('id') ?? '';
  const d = p.getAttribute('d') ?? '';
  out.edges.push({
    id,
    from: '',  // filled by heuristic below
    to: '',
    d,
    points: decodeD(d),
    marker_end: (p.getAttribute('marker-end') ?? '').match(/-(\w+)End$/) ? 'barb' : '',
  });
}

// ---- edge labels -----------------------------------------------------------
for (const g of doc.querySelectorAll('g.edgeLabels > g.edgeLabel')) {
  const tr = (g.getAttribute('transform') ?? '').match(
    /translate\(\s*([-\d.eE]+)[, ]+([-\d.eE]+)\s*\)/);
  const fo = g.querySelector('foreignObject');
  const text = (fo?.textContent ?? '').trim();
  if (!text) continue;
  out.edge_labels.push({
    kind: 'label',
    text,
    x: tr ? Number(tr[1]) : 0,
    y: tr ? Number(tr[2]) : 0,
    w: fo ? Number(fo.getAttribute('width')) : 0,
    h: fo ? Number(fo.getAttribute('height')) : 0,
  });
}

writeFileSync(outPath, JSON.stringify(out, null, 2));
console.log(`[state] nodes=${out.nodes.length} edges=${out.edges.length} labels=${out.edge_labels.length} canvas=${out.canvas.vbwidth}x${out.canvas.vbheight}`);