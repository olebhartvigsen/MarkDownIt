// For every class golden edge: try basis-spline over dagre polyline with
// endpoint trims from {0, 6, 18} per side; report which combination
// reproduces the golden d exactly.
import { readFileSync } from 'node:fs';
import { line, curveBasis } from 'd3-shape';

const names = ['cls1', 'cls2', 'cls3', 'cls4', 'cls5'];
const trims = [0, 3, 5, 6, 9, 10, 17, 18, 20];

function norm(dx, dy) { return Math.hypot(dx, dy); }

for (const name of names) {
  const golden = JSON.parse(readFileSync(`/workspace/MarkDownIt/tests/mermaid/golden/${name}.json`, 'utf8'));
  for (const e of golden.edges) {
    // dagre polyline: start = source border, end = target border, mids = dummy centers
    // rebuild from golden d? No — we need dagre points. Use the oracle dump? Instead:
    // the polyline = golden points (they ARE a spline decode...). Use BK trace values:
    // skip: we take dagre points from /tmp/clsN.json oracle run? Those are also rendered.
    // Fallback: reconstruct candidate polyline by inverting is complex; instead test:
    // does basis(points with trimmed endpoints) == golden d, where points =
    // [p0, mids..., pn] with p0/pn from the known dagre output printed by BK trace.
    console.log(name, e.id, 'golden pts', JSON.stringify(e.points.map(p => [p.x, p.y])));
  }
}
