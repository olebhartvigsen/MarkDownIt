// Class-diagram oracle: render one fixture headless via the dom-shim and dump
// the geometric facts the native renderer must reproduce, as JSON.
// Structure produced by mermaid 11.7 classRenderer-v3-unified + dagre wrapper:
//   - canvas:      svg width/height/viewBox (startx/starty = viewBox mins)
//   - nodes:       g.node default groups. Basic label-container fill path gives
//                  the box (w, h from -w/2..w/2, -h/2..h/2). Group translates
//                  give label/members/methods text positions; divider paths
//                  give divider y's. Widths come from foreignObject attrs.
//   - edges:       g.edgePaths path elements: id `id_<from>_<to>_<n>`,
//                  marker-end/marker-start urls, label, and the d string.
//                  d points are ABSOLUTE viewBox coordinates. The d string is
//                  a basis-spline over the dagre polyline (M/L/C segments); we
//                  emit the raw d plus the decoded M/L/C endpoint polyline.
//   - edgeLabels:  g.edgeLabels g.edgeLabel translate(x,y) + label text and
//                  foreignObject w/h; cardinality terminals g.edgeTerminals.
import { readFileSync, writeFileSync } from 'node:fs';
import { renderDiagram } from './oracle_util.mjs';

const srcPath = process.argv[2];
const outPath = process.argv[3];
if (!srcPath || !outPath) {
  console.error('usage: node class_oracle.mjs <fixture.mmd> <out.json>');
  process.exit(2);
}

const src = readFileSync(srcPath, 'utf8');
const { svg } = await renderDiagram('class', src, {});

const doc = new DOMParser().parseFromString(svg, 'image/svg+xml');
const svgEl = doc.querySelector('svg');
const viewBox = (svgEl.getAttribute('viewBox') ?? '').split(/\s+/).map(Number);
// The v3 class renderer delegates to the state config for useMaxWidth, so the
// svg width attr is `100%` with a max-width style even with the override.
// canvas width/height therefore fall back to the viewBox dims (what a real
// browser scales to).
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

// ---- nodes ------------------------------------------------------------------
// Each class renders as g.node.default with id `classId-<Name>-<n>` and
// transform translate(x, y) (the dagre node center). Children:
//   g.basic.label-container > path[fill="#ECECFF"] (the box, in local coords)
//   g.annotation-group.text / g.label-group.text / g.members-group.text /
//   g.methods-group.text with transform translate(tx, ty); inside, per-text
//   g.label with transform translate(0, lineY) and a foreignObject whose
//   width/height attrs carry the html label size (8px/char, 20px line under
//   the shim).
//   g.divider paths with d="M0 <y> ..." (local divider y).
for (const nodeEl of doc.querySelectorAll('g.node')) {
  const rawId = nodeEl.getAttribute('id') ?? '';
  const m = rawId.match(/^classId-(.+)-\d+$/);
  const id = m ? m[1] : rawId;
  const tr = (nodeEl.getAttribute('transform') ?? '').match(
    /translate\(\s*([-\d.eE]+)[, ]+([-\d.eE]+)\s*\)/);
  const node = {
    id,
    cx: tr ? Number(tr[1]) : 0,
    cy: tr ? Number(tr[2]) : 0,
    w: 0, h: 0,
    title: '',
    annotations: [],
    members: [],
    methods: [],
    member_x: [],   // local text x (group tx + inner translate x)
    member_y: [],   // local label y (group ty + inner translate y)
    method_x: [],
    method_y: [],
    annotation_y: null,
    annotation_w: 0,
    label_y: null,
    label_x: null,
    label_w: 0,
    divider_y: [],
  };

  const fillPath = nodeEl.querySelector('.basic.label-container path[fill]');
  if (fillPath) {
    // d="M<w/2> -<h/2> L..." style: first M x,y then L x,y x,y x,y.
    const nums = (fillPath.getAttribute('d') ?? '').match(/-?[\d.]+/g) ?? [];
    if (nums.length >= 2) {
      const wHalf = Math.abs(Number(nums[0]));
      const hHalf = Math.abs(Number(nums[1]));
      node.w = wHalf * 2;
      node.h = hHalf * 2;
    }
  }

  const grabTexts = (sel) => {
    const g = nodeEl.querySelector(sel);
    if (!g) return null;
    const gt = (g.getAttribute('transform') ?? '').match(
      /translate\(\s*([-\d.eE]+)(?:[, ]+([-\d.eE]+))?\s*\)/);
    const tx = gt ? Number(gt[1]) : 0;
    const ty = gt ? Number(gt[2]) : 0;
    const texts = [];
    for (const lbl of g.querySelectorAll(':scope > g.label')) {
      const lt = (lbl.getAttribute('transform') ?? '').match(
        /translate\(\s*([-\d.eE]+)(?:[, ]+([-\d.eE]+))?\s*\)/);
      const fo = lbl.querySelector('foreignObject');
      texts.push({
        text: (fo?.textContent ?? '').trim(),
        x: tx + (lt ? Number(lt[1]) : 0),
        y: ty + (lt ? Number(lt[2]) : 0),
        w: fo ? Number(fo.getAttribute('width')) : 0,
        h: fo ? Number(fo.getAttribute('height')) : 0,
      });
    }
    return texts;
  };

  const ann = grabTexts('.annotation-group');
  const lbl = grabTexts('.label-group');
  const mem = grabTexts('.members-group');
  const mth = grabTexts('.methods-group');
  if (ann) for (const t of ann) {
    node.annotations.push(t.text);
    node.annotation_y = t.y;
    node.annotation_w = t.w;
  }
  if (lbl) {
    node.label_x = lbl.length ? lbl[0].x : null;
    node.label_y = lbl.length ? lbl[0].y : null;
    node.label_w = lbl.length ? lbl[0].w : 0;
    if (lbl.length) node.title = lbl[0].text;
  }
  if (mem) for (const t of mem) {
    node.members.push(t.text);
    node.member_x.push(t.x);
    node.member_y.push(t.y);
  }
  if (mth) for (const t of mth) {
    node.methods.push(t.text);
    node.method_x.push(t.x);
    node.method_y.push(t.y);
  }

  for (const d of nodeEl.querySelectorAll('g.divider path')) {
    const dm = (d.getAttribute('d') ?? '').match(/M\s*(-?[\d.]+)\s+(-?[\d.]+)/);
    if (dm) node.divider_y.push(Number(dm[2]));
  }

  out.nodes.push(node);
}

// ---- edges ------------------------------------------------------------------
// g.edgePaths > path with id `id_<from>_<to>_<n>`; class carries the pattern
// (solid/dashed/dotted); marker-end/marker-start url(#g_class-<type>End|Start).
// The d string is a d3 basis-spline over the polyline, absolute coords.
const decodeD = (d) => {
  // Return every segment endpoint (M/L endpoints and C final points) in order;
  // C control points are consumed but the C++ renderer emits the identical
  // basis spline from the same polyline, so endpoints suffice for the gate.
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
  const idm = id.match(/^id_(.+?)_(.+?)_(\d+)$/);
  const markerEnd = (p.getAttribute('marker-end') ?? '').match(/#g_class-(\w+?)End/);
  const markerStart = (p.getAttribute('marker-start') ?? '').match(/#g_class-(\w+?)Start/);
  const d = p.getAttribute('d') ?? '';
  out.edges.push({
    id,
    from: idm ? idm[1] : '',
    to: idm ? idm[2] : '',
    start_marker: markerStart ? markerStart[1] : '',
    end_marker: markerEnd ? markerEnd[1] : '',
    pattern: (p.getAttribute('class') ?? '').includes('dashed') ? 'dashed'
      : (p.getAttribute('class') ?? '').includes('dotted') ? 'dotted' : 'solid',
    d,
    points: decodeD(d),
  });
}

// ---- edge labels (relation labels + cardinality terminals) -------------------
// g.edgeLabels > g.edgeLabel (one per edge, translate(x, y) = label center);
// inside g.label > foreignObject (w/h) with the text. Terminal labels live in
// g.edgeTerminals groups: svg text elements with the raw string, positioned
// via g.inner translate(-w/2,-h/2) at translate(x, y).
for (const g of doc.querySelectorAll('g.edgeLabels > g.edgeLabel')) {
  const tr = (g.getAttribute('transform') ?? '').match(
    /translate\(\s*([-\d.eE]+)[, ]+([-\d.eE]+)\s*\)/);
  const fo = g.querySelector('foreignObject');
  const text = (fo?.textContent ?? '').trim();
  if (!text) continue;  // mermaid emits an empty label group per edge
  out.edge_labels.push({
    kind: 'label',
    text,
    x: tr ? Number(tr[1]) : 0,
    y: tr ? Number(tr[2]) : 0,
    w: fo ? Number(fo.getAttribute('width')) : 0,
    h: fo ? Number(fo.getAttribute('height')) : 0,
  });
}
for (const g of doc.querySelectorAll('g.edgeTerminals')) {
  const tr = (g.getAttribute('transform') ?? '').match(
    /translate\(\s*([-\d.eE]+)[, ]+([-\d.eE]+)\s*\)/);
  const inner = g.querySelector('g.inner');
  const it = inner ? (inner.getAttribute('transform') ?? '').match(
    /translate\(\s*([-\d.eE]+)[, ]+([-\d.eE]+)\s*\)/) : null;
  const text = (g.textContent ?? '').trim();
  const bx = inner ? inner.getBBox() : { width: 0, height: 0 };
  out.edge_labels.push({
    kind: 'terminal',
    text,
    x: tr ? Number(tr[1]) : 0,
    y: tr ? Number(tr[2]) : 0,
    w: bx.width,
    h: bx.height,
    ix: it ? Number(it[1]) : 0,
    iy: it ? Number(it[2]) : 0,
  });
}

writeFileSync(outPath, JSON.stringify(out, null, 2));
console.log(`[class] nodes=${out.nodes.length} edges=${out.edges.length} labels=${out.edge_labels.length} canvas=${out.canvas.vbwidth}x${out.canvas.vbheight}`);
