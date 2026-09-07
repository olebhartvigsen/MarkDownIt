// render_seq_svg.mjs
// Turn a sequence golden JSON (seq1..seq5) into an SVG preview showing the
// geometry the native Direct2D renderer must reproduce.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const REPO = path.resolve(HERE, '..', '..');
const GOLDEN_DIR = path.join(REPO, 'tests', 'mermaid', 'golden');
const FIX_DIR = path.join(REPO, 'tests', 'mermaid', 'fixtures');
const OUT_DIR = path.join(HERE, 'out');

const esc = s => String(s).replace(/[&<>"']/g,
  c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));

const ACTOR_FILL = '#ECECFF', ACTOR_STROKE = '#9370DB';
const INK = '#333', NOTE_FILL = '#ffffde';

function renderSeq(g) {
  const { canvas } = g;
  // viewBox space: startx..startx+vbwidth, starty..starty+vbheight.
  // SVG y=0 is viewBox top; golden coords are absolute in that space.
  const ox = -canvas.startx, oy = -canvas.starty;
  const W = canvas.vbwidth, H = canvas.vbheight;
  const parts = [];
  parts.push(`<rect width="${W}" height="${H}" fill="#fdfdff"/>`);

  // backgrounds (rect fills)
  for (const b of g.backgrounds || []) {
    parts.push(`<rect x="${b.x+ox}" y="${b.y+oy}" width="${b.w}" height="${b.h}" fill="${esc(b.fill)}" fill-opacity="0.5"/>`);
  }
  // loops: border + label box + texts + section dividers
  for (const l of g.loops || []) {
    if (l.loopLineCount !== undefined) continue;  // count-only entry
    if (l.top !== undefined) {  // border segment: draw as one line
      parts.push(`<line x1="${l.x1+ox}" y1="${l.y1+oy}" x2="${l.x2+ox}" y2="${l.y2+oy}" stroke="${INK}" stroke-width="1"/>`);
      continue;
    }
    parts.push(`<rect x="${l.startx+ox}" y="${l.starty+oy}" width="${l.stopx-l.startx}" height="${l.stopy-l.starty}" fill="none" stroke="${INK}" stroke-width="1"/>`);
    if (l.label) {
      parts.push(`<rect x="${l.startx+ox}" y="${l.starty+oy}" width="60" height="20" fill="${ACTOR_FILL}" stroke="${INK}" stroke-width="1"/>`);
      parts.push(`<text x="${l.startx+ox+30}" y="${l.starty+oy+14}" text-anchor="middle" font-family="sans-serif" font-size="12" fill="${INK}">${esc(l.label)}</text>`);
    }
    if (l.title) {
      parts.push(`<text x="${l.startx+ox+70}" y="${l.starty+oy+14}" font-family="sans-serif" font-size="12" fill="${INK}">${esc(l.title)}</text>`);
    }
    (l.section_y || []).forEach((sy, i) => {
      parts.push(`<line x1="${l.startx+ox}" y1="${sy+oy}" x2="${l.stopx+ox}" y2="${sy+oy}" stroke="${INK}" stroke-dasharray="3 3"/>`);
      const t = (l.section_titles || [])[i];
      if (t) parts.push(`<text x="${l.startx+ox+70}" y="${sy+oy+14}" font-family="sans-serif" font-size="12" fill="${INK}">${esc(t)}</text>`);
    });
  }
  // lifelines
  for (const a of g.actors || []) {
    if (a.kind === 'lifeline') {
      parts.push(`<line x1="${a.x+ox}" y1="${a.y1+oy}" x2="${a.x+ox}" y2="${a.y2+oy}" stroke="#666" stroke-dasharray="4 3"/>`);
    }
  }
  // activations
  for (const a of g.activations || []) {
    parts.push(`<rect x="${a.x+ox}" y="${a.y+oy}" width="${a.w}" height="${a.h}" fill="${ACTOR_FILL}" stroke="${ACTOR_STROKE}"/>`);
  }
  // messages
  for (const m of g.messages || []) {
    const dotted = ((m.line && m.line.cls) || (m.path && m.path.cls) || '').includes('messageLine1');
    const dash = dotted ? ' stroke-dasharray="5 3"' : '';
    if (m.path) {
      // self loop: translate M x,y by (ox,oy)
      const d = (m.path.d || '').replace(/([MC])\s*([-\d.,\s]+)/g, (_, cmd, nums) => {
        const ns = nums.trim().split(/[\s,]+/).map(Number);
        const out = [];
        for (let i = 0; i < ns.length; i += 2) out.push(`${(ns[i]+ox).toFixed(1)},${(ns[i+1]+oy).toFixed(1)}`);
        return cmd + out.join(' ');
      });
      parts.push(`<path d="${d}" fill="none" stroke="${INK}" stroke-width="1.5"${dash}/>`);
      const mEnd = d.match(/([-\d.]+),([-\d.]+)\s*$/);
      if (mEnd) {
        const ex = Number(mEnd[1]), ey = Number(mEnd[2]);
        parts.push(`<polygon points="${ex},${ey} ${ex-10},${ey-4} ${ex-10},${ey+4}" fill="${INK}"/>`);
      }
    } else if (m.line) {
      const {x1,y1,x2,y2} = m.line;
      parts.push(`<line x1="${x1+ox}" y1="${y1+oy}" x2="${x2+ox}" y2="${y2+oy}" stroke="${INK}" stroke-width="1.5"${dash}/>`);
      const dir = x2 >= x1 ? 1 : -1;
      if (dotted) {
        parts.push(`<polyline points="${x2+ox},${y2+oy} ${x2+ox-10*dir},${y2+oy-5} ${x2+ox-10*dir},${y2+oy+5}" fill="none" stroke="${INK}" stroke-width="1.5"/>`);
      } else {
        parts.push(`<polygon points="${x2+ox},${y2+oy} ${x2+ox-11*dir},${y2+oy-5} ${x2+ox-11*dir},${y2+oy+5}" fill="${INK}"/>`);
      }
    }
    if (m.text) {
      parts.push(`<text x="${m.tx+ox}" y="${m.ty+oy}" text-anchor="middle" font-family="sans-serif" font-size="14" fill="${INK}">${esc(m.text)}</text>`);
    }
  }
  // notes
  for (const n of g.notes || []) {
    parts.push(`<rect x="${n.rx+ox}" y="${n.ry+oy}" width="${n.rw}" height="${n.rh}" rx="4" fill="${NOTE_FILL}" stroke="${ACTOR_STROKE}"/>`);
    parts.push(`<text x="${n.tx+ox}" y="${n.ty+oy}" text-anchor="middle" font-family="sans-serif" font-size="14" fill="${INK}">${esc(n.text)}</text>`);
  }
  // actor boxes (top + bottom) drawn after lines so they sit on top
  for (const a of g.actors || []) {
    if (a.kind === 'lifeline') continue;
    parts.push(`<rect x="${a.x+ox}" y="${a.y+oy}" width="${a.w}" height="${a.h}" fill="${ACTOR_FILL}" stroke="${ACTOR_STROKE}"/>`);
    parts.push(`<text x="${a.x+ox+a.w/2}" y="${a.y+oy+a.h/2+5}" text-anchor="middle" font-family="sans-serif" font-size="14" font-weight="bold" fill="${INK}">${esc(a.name)}</text>`);
  }
  // autonumber circles
  for (const n of g.numbers || []) {
    parts.push(`<circle cx="${n.x+ox+8}" cy="${n.y+oy}" r="9" fill="${INK}"/>`);
    parts.push(`<text x="${n.x+ox+8}" y="${n.y+oy+4}" text-anchor="middle" font-family="sans-serif" font-size="11" fill="#fff">${n.n}</text>`);
  }
  return `<svg xmlns="http://www.w3.org/2000/svg" width="${W}" height="${H}" viewBox="0 0 ${W} ${H}">\n  ${parts.join('\n  ')}\n</svg>\n`;
}

function main() {
  fs.mkdirSync(OUT_DIR, { recursive: true });
  const rows = [];
  for (const s of [1,2,3,4,5]) {
    const gpath = path.join(GOLDEN_DIR, `seq${s}.json`);
    const fpath = path.join(FIX_DIR, `seq${s}.mmd`);
    if (!fs.existsSync(gpath)) continue;
    const golden = JSON.parse(fs.readFileSync(gpath, 'utf8'));
    const svg = renderSeq(golden);
    fs.writeFileSync(path.join(OUT_DIR, `seq${s}.svg`), svg);
    rows.push({ name: `seq${s}`, src: fs.existsSync(fpath) ? fs.readFileSync(fpath, 'utf8') : '' });
    console.log(`wrote out/seq${s}.svg`);
  }
  const html = `<!doctype html>\n<html><head><meta charset="utf-8"><title>Sequence oracle preview</title>\n<style>body{font-family:sans-serif;background:#f6f6fa;margin:20px;color:#222}h2{font-size:15px;margin-top:32px}.row{display:flex;gap:24px;background:#fff;padding:16px;border:1px solid #ddd;border-radius:6px}pre{background:#f0f0f4;padding:10px;border-radius:4px;font-size:12px;margin:0;min-width:260px}img{border:1px solid #eee;background:#fff}</style></head><body>\n<h1>Sequence oracle SVG preview</h1>\n<p>Rendered from the committed goldens (mermaid.js geometry via the dom-shim oracle). This is what the native renderer must reproduce.</p>\n` +
    rows.map(r => `<h2>${r.name}</h2>\n<div class="row"><pre>${esc(r.src)}</pre><img src="${r.name}.svg"/></div>`).join('\n') +
    `\n</body></html>\n`;
  fs.writeFileSync(path.join(OUT_DIR, 'index.html'), html);
  console.log('wrote out/index.html');
}
main();
