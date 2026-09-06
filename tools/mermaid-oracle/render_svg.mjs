// render_svg.mjs
// Turn a golden JSON (oracle output = ground-truth dagre geometry) into an
// SVG showing exactly the layout the C++ engine must reproduce. Also emits
// an HTML index with all fixtures side by side.
//
// Usage:
//   node render_svg.mjs                       (all fixtures -> out/*.svg + index.html)
//   node render_svg.mjs 02-shapes-edges       (one fixture)

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const REPO = path.resolve(HERE, '..', '..');
const GOLDEN_DIR = path.join(REPO, 'tests', 'mermaid', 'golden');
const FIX_DIR = path.join(REPO, 'tests', 'mermaid', 'fixtures');
const OUT_DIR = path.join(HERE, 'out');

const PAD = 20;
const NODE_FILL = '#ECECFF';
const NODE_STROKE = '#9370DB';
const TEXT = '#111';
const EDGE = '#333';

function esc(s) {
  return String(s).replace(/[&<>"']/g, c =>
    ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
}

// Try to infer shape from label brackets in the source fixture: a bare id
// gives Rect; the actual shape lives in the .mmd, not in the golden. We
// parse the .mmd once to build id -> shape map.
function parseShapes(srcPath) {
  const shapes = new Map();
  if (!fs.existsSync(srcPath)) return shapes;
  const src = fs.readFileSync(srcPath, 'utf8');
  // Order matters: longest bracket forms first.
  const patterns = [
    [/([A-Za-z0-9_]+)\(\[([^\]]*)\]\)/g, 'stadium'],
    [/([A-Za-z0-9_]+)\(\(([^)]*)\)\)/g, 'circle'],
    [/([A-Za-z0-9_]+)\{([^}]*)\}/g,     'diamond'],
    [/([A-Za-z0-9_]+)\[([^\]]*)\]/g,    'rect'],
    [/([A-Za-z0-9_]+)\(([^)]*)\)/g,     'round'],
  ];
  for (const [re, shape] of patterns) {
    let m;
    while ((m = re.exec(src)) !== null) {
      if (!shapes.has(m[1])) shapes.set(m[1], shape);
    }
  }
  return shapes;
}

function drawNode(n, shape) {
  const x = n.x - n.width / 2 + PAD;
  const y = n.y - n.height / 2 + PAD;
  const w = n.width, h = n.height;
  const cx = n.x + PAD, cy = n.y + PAD;
  const label = esc(n.label || n.id);
  const common = `fill="${NODE_FILL}" stroke="${NODE_STROKE}" stroke-width="1"`;
  let shapeSvg = '';
  switch (shape) {
    case 'round':
      shapeSvg = `<rect x="${x}" y="${y}" width="${w}" height="${h}" rx="8" ry="8" ${common}/>`;
      break;
    case 'stadium':
      shapeSvg = `<rect x="${x}" y="${y}" width="${w}" height="${h}" rx="${h / 2}" ry="${h / 2}" ${common}/>`;
      break;
    case 'diamond': {
      const pts = [
        [cx,     cy - h / 2],
        [cx + w / 2, cy],
        [cx,     cy + h / 2],
        [cx - w / 2, cy],
      ].map(p => p.join(',')).join(' ');
      shapeSvg = `<polygon points="${pts}" ${common}/>`;
      break;
    }
    case 'circle':
      shapeSvg = `<ellipse cx="${cx}" cy="${cy}" rx="${w / 2}" ry="${h / 2}" ${common}/>`;
      break;
    case 'rect':
    default:
      shapeSvg = `<rect x="${x}" y="${y}" width="${w}" height="${h}" ${common}/>`;
      break;
  }
  const text = `<text x="${cx}" y="${cy}" text-anchor="middle" dominant-baseline="central" font-family="sans-serif" font-size="14" fill="${TEXT}">${label}</text>`;
  const idTag = `<text x="${x + 4}" y="${y + 12}" font-family="monospace" font-size="9" fill="#888">${esc(n.id)}</text>`;
  return shapeSvg + text + idTag;
}

function drawEdge(e) {
  if (!e.points || e.points.length < 2) return '';
  const pts = e.points.map(p => `${p.x + PAD},${p.y + PAD}`).join(' ');
  const last = e.points[e.points.length - 1];
  const prev = e.points[e.points.length - 2];
  const dx = last.x - prev.x, dy = last.y - prev.y;
  const len = Math.hypot(dx, dy) || 1;
  const ux = dx / len, uy = dy / len;
  const ax = last.x + PAD, ay = last.y + PAD;
  const ah = 10, aw = 6;
  const p1 = `${ax},${ay}`;
  const p2 = `${ax - ah * ux + aw * -uy},${ay - ah * uy + aw * ux}`;
  const p3 = `${ax - ah * ux - aw * -uy},${ay - ah * uy - aw * ux}`;
  let label = '';
  if (e.label) {
    const mid = e.points[Math.floor(e.points.length / 2)];
    label = `<text x="${mid.x + PAD + 4}" y="${mid.y + PAD - 4}" font-family="sans-serif" font-size="11" fill="#555">${esc(e.label)}</text>`;
  }
  return (
    `<polyline points="${pts}" fill="none" stroke="${EDGE}" stroke-width="1.5"/>` +
    `<polygon points="${p1} ${p2} ${p3}" fill="${EDGE}"/>` +
    label
  );
}

function renderSvg(golden, shapes) {
  const w = golden.graph.width + PAD * 2;
  const h = golden.graph.height + PAD * 2;
  const bg = `<rect width="${w}" height="${h}" fill="#fdfdff"/>`;
  const edges = (golden.edges || []).map(drawEdge).join('\n  ');
  const nodes = (golden.nodes || [])
    .map(n => drawNode(n, shapes.get(n.id) || 'rect'))
    .join('\n  ');
  return (
    `<svg xmlns="http://www.w3.org/2000/svg" width="${w}" height="${h}" viewBox="0 0 ${w} ${h}">\n` +
    `  ${bg}\n  ${edges}\n  ${nodes}\n</svg>\n`
  );
}

function main() {
  fs.mkdirSync(OUT_DIR, { recursive: true });
  const arg = process.argv[2];
  const goldenFiles = arg
    ? [arg.endsWith('.json') ? arg : arg + '.json']
    : fs.readdirSync(GOLDEN_DIR).filter(f => f.endsWith('.json')).sort();

  const rows = [];
  for (const gf of goldenFiles) {
    const gpath = path.join(GOLDEN_DIR, gf);
    if (!fs.existsSync(gpath)) {
      console.error(`SKIP missing: ${gpath}`);
      continue;
    }
    const golden = JSON.parse(fs.readFileSync(gpath, 'utf8'));
    const base = gf.replace(/\.json$/, '');
    const shapes = parseShapes(path.join(FIX_DIR, base + '.mmd'));
    const svg = renderSvg(golden, shapes);
    const outSvg = path.join(OUT_DIR, base + '.svg');
    fs.writeFileSync(outSvg, svg);
    const src = fs.existsSync(path.join(FIX_DIR, base + '.mmd'))
      ? fs.readFileSync(path.join(FIX_DIR, base + '.mmd'), 'utf8')
      : '';
    rows.push({ base, src });
    console.log(`wrote ${path.relative(REPO, outSvg)}`);
  }

  const html =
    `<!doctype html>\n<html><head><meta charset="utf-8"><title>Mermaid oracle SVG preview</title>\n` +
    `<style>body{font-family:sans-serif;background:#f6f6fa;margin:20px;color:#222}\n` +
    `h1{font-size:20px}h2{font-size:15px;margin-top:32px}\n` +
    `.row{display:flex;gap:24px;align-items:flex-start;background:#fff;padding:16px;border:1px solid #ddd;border-radius:6px}\n` +
    `pre{background:#f0f0f4;padding:10px;border-radius:4px;font-size:12px;margin:0;min-width:220px}\n` +
    `img{border:1px solid #eee;background:#fff}\n</style></head><body>\n` +
    `<h1>Mermaid oracle SVG preview</h1>\n` +
    `<p>Each SVG is rendered directly from the oracle golden JSON (dagre-d3-es geometry). This is exactly what the C++ Direct2D renderer must reproduce on Windows: same node positions, sizes, shapes, and edge polylines. Font metrics will differ by a few DIPs; layout structure will not.</p>\n` +
    rows.map(r =>
      `<h2>${esc(r.base)}</h2>\n<div class="row">\n` +
      `<pre>${esc(r.src)}</pre>\n<img src="${esc(r.base)}.svg" alt="${esc(r.base)}"/>\n</div>`
    ).join('\n') +
    `\n</body></html>\n`;
  fs.writeFileSync(path.join(OUT_DIR, 'index.html'), html);
  console.log(`wrote ${path.relative(REPO, path.join(OUT_DIR, 'index.html'))}`);
}

main();
