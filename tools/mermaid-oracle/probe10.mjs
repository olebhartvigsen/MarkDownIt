// Verify the class edge pipeline against goldens:
// polyline = [source.border(firstMid), mid..., target.border(lastMid)]
// where border = rect intersection of the border-box with the direction to
// the neighbor; rendered d = basis spline over [trimmed polyline]. Trims:
// start trim only if start_marker, end trim only if end_marker.
import { readFileSync } from 'node:fs';
import { line, curveBasis } from 'd3-shape';

const base = '/workspace/MarkDownIt/tests/mermaid/';

// known dagre outputs (from BK trace, /tmp/clsN.json reruns)
const dagre = {
  cls1: { 'Dog|Animal': [[100, 80], [100, 105], [100, 130]] },
  cls2: { 'Square|Shape': [[116, 44], [201, 44], [264.4230769230769, 78]],
          'Circle|Shape': [[176, 148], [201, 148], [264.4230769230769, 114]] },
  cls3: { 'Car|Engine': [[89.35384615384615, 56], [72, 103], [72, 150]],
          'Car|Wheel': [[118.43076923076923, 56], [177, 103], [222.76923076923077, 138]],
          'Driver|Wheel': [[290, 68], [290, 103], [274.9230769230769, 138]] },
  cls4: { 'App|Logger': [[176.0943396226415, 44], [116, 79], [116, 132]],
          'App|Config': [[237.9056603773585, 44], [298, 79], [298, 114]] },
  cls5: { 'SqlRepository|Repository': [[136, 150], [136, 175], [284.83720930232556, 200]],
          'MemoryRepository|Repository': [[392, 150], [392, 175], [392, 200]],
          'Migration|Repository': [[399.2358490566038, 44], [529, 79], [529, 132], [529, 175], [449.3488372093023, 200]],
          'Migration|SqlRepository': [[265.7641509433962, 44], [136, 79], [136, 114]] },
};

// border box = shape w/h + 2*pad(12); rect intersect from border-box center
function borderRect(cx, cy, w, h) { return { cx, cy, w, h }; }
function intersect(rect, outside) {
  const { cx, cy, w, h } = rect;
  const dx = outside.x - cx, dy = outside.y - cy;
  const inf = Infinity;
  const tx = dx !== 0 ? (w / 2) / Math.abs(dx) : inf;
  const ty = dy !== 0 ? (h / 2) / Math.abs(dy) : inf;
  const t = Math.min(tx, ty);
  return { x: cx + dx * t, y: cy + dy * t };
}
function clipPolyline(pts, dist, fromStart) {
  // walk `dist` along polyline from the chosen end, cut there, return rest
  const p = pts.map(q => ({ ...q }));
  let segs = [];
  for (let i = 0; i + 1 < p.length; i++) segs.push([p[i], p[i + 1]]);
  let total = 0;
  const segLens = segs.map(s => norm(s[1].x - s[0].x, s[1].y - s[0].y));
  const target = fromStart ? dist : dist; // measure from start or end
  let acc = 0, cutAt = -1, frac = 0;
  const order = fromStart ? segs : segs.slice().reverse();
  for (let i = 0; i < order.length; i++) {
    const L = segLens[fromStart ? i : segs.length - 1 - i];
    if (acc + L >= target) { frac = (target - acc) / L; cutAt = fromStart ? i : segs.length - 1 - i; break; }
    acc += L;
  }
  if (cutAt < 0) return p;
  const [a, b] = segs[cutAt];
  const L = segLens[cutAt];
  const cutFrac = fromStart ? ((target - acc) / L) : (1 - (target - acc) / L);
  const cut = { x: a.x + (b.x - a.x) * cutFrac, y: a.y + (b.y - a.y) * cutFrac };
  if (fromStart) return [cut, ...p.slice(cutAt + 1)];
  return [...p.slice(0, cutAt + 1), cut];
}

// node border sizes from goldens
const gold = {};
for (let i = 1; i <= 5; i++) gold[`cls${i}`] = JSON.parse(readFileSync(base + `golden/cls${i}.json`, 'utf8'));
const nodeWH = {};
for (const [k, g] of Object.entries(gold)) for (const n of g.nodes) nodeWH[`${k}|${n.id}`] = { w: n.w, h: n.h };
const nodeXY = {};
for (const [k, g] of Object.entries(gold)) for (const n of g.nodes) nodeXY[`${k}|${n.id}`] = { x: n.cx, y: n.cy };

const markerByEdge = {};
for (const [k, g] of Object.entries(gold)) for (const e of g.edges) markerByEdge[`${k}|${e.from}|${e.to}`] = { sm: e.start_marker, em: e.end_marker, pattern: e.pattern };

let fails = 0, total = 0;
for (const name of Object.keys(dagre)) {
  const g = gold[name];
  for (const e of g.edges) {
    const key = `${name}|${e.from}|${e.to}`;
    const pts = dagre[name][`${e.from}|${e.to}`].map(([x, y]) => ({ x, y }));
    if (!pts) { console.log('NO DAGRE PTS', key); continue; }
    total++;
    const mk = markerByEdge[key];
    const uC = nodeXY[`${name}|${e.from}`], vC = nodeXY[`${name}|${e.to}`];
    const uS = nodeWH[`${name}|${e.from}`], vS = nodeWH[`${name}|${e.to}`];
    const firstMid = pts[1], lastMid = pts[pts.length - 2];
    const start = intersect(borderRect(uC.x, uC.y, uS.w, uS.h), { x: firstMid.x, y: firstMid.y });
    const end = intersect(borderRect(vC.x, vC.y, vS.w, vS.h), { x: lastMid.x, y: lastMid.y });
    let poly = [start, ...pts.slice(1, -1), end];
    if (mk.sm) poly = clipPolyline(poly, 18, true);
    const endTrim = mk.em === 'extension' ? 18 : mk.em === 'dependency' ? 6 : 0;
    if (endTrim > 0) poly = clipPolyline(poly, endTrim, false);
    const d = line().curve(curveBasis)(poly.map(p => [p.x, p.y]));
    const got = d.replace(/(\.\d\d)\d+/g, '$1'); // coarse
    if (d !== e.d) {
      fails++;
      console.log('MISMATCH', key);
      console.log('  poly', JSON.stringify(poly));
      console.log('  ours', d);
      console.log('  gold', e.d);
    }
  }
}
console.log(`=== ${total - fails}/${total} edge d strings matched ===`);

function norm(dx, dy) { return Math.hypot(dx, dy); }
