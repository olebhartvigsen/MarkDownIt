// Full reference extraction from a REAL browser-exported mermaid SVG.
// Usage: node svg_extract.mjs <real.svg> <out.json>
import { readFileSync, writeFileSync } from 'node:fs';
import { JSDOM } from 'jsdom';

const svgPath = process.argv[2];
const outPath = process.argv[3];
if (!svgPath || !outPath) { console.error('usage: node svg_extract.mjs <svg> <json>'); process.exit(2); }
const svg = readFileSync(svgPath, 'utf8');
const doc = new JSDOM(svg).window.document;
const svgEl = doc.querySelector('svg');
const N = (v) => v == null ? null : Number(v);

const out = {
  canvas: {
    width: N(svgEl.getAttribute('width')),
    height: N(svgEl.getAttribute('height')),
    viewBox: svgEl.getAttribute('viewBox'),
  },
  actors: [], messages: [], notes: [], loops: [], numbers: [], backgrounds: [],
  stickmen: [],
};

for (const r of doc.querySelectorAll('rect.actor')) {
  out.actors.push({
    bottom: r.classList.contains('actor-bottom'),
    name: r.getAttribute('name'),
    x: N(r.getAttribute('x')), y: N(r.getAttribute('y')),
    w: N(r.getAttribute('width')), h: N(r.getAttribute('height')),
  });
}
for (const g of doc.querySelectorAll('g.actor-man')) {
  const circle = g.querySelector('circle');
  out.stickmen.push({
    bottom: g.classList.contains('actor-bottom'),
    name: g.getAttribute('name'),
    cx: N(circle.getAttribute('cx')), cy: N(circle.getAttribute('cy')),
  });
}
for (const l of doc.querySelectorAll('line.actor-line')) {
  out.actors.push({ kind: 'lifeline', name: l.getAttribute('name'),
    x: N(l.getAttribute('x1')), y1: N(l.getAttribute('y1')), y2: N(l.getAttribute('y2')) });
}

const els = doc.querySelectorAll('text.messageText, line.messageLine0, line.messageLine1, path.messageLine0, path.messageLine1');
let pendingText = null;
for (const el of els) {
  const tag = el.tagName.toLowerCase();
  if (tag === 'text') { pendingText = el; continue; }
  if (!pendingText) continue;
  const msg = {
    text: pendingText.textContent,
    tx: N(pendingText.getAttribute('x')), ty: N(pendingText.getAttribute('y')),
  };
  if (tag === 'line') {
    msg.line = { x1: N(el.getAttribute('x1')), y1: N(el.getAttribute('y1')),
      x2: N(el.getAttribute('x2')), y2: N(el.getAttribute('y2')),
      cls: el.getAttribute('class') };
  } else {
    msg.path = { d: el.getAttribute('d'), cls: el.getAttribute('class') };
  }
  out.messages.push(msg);
}
for (const r of doc.querySelectorAll('rect.note')) {
  out.notes.push({ x: N(r.getAttribute('x')), y: N(r.getAttribute('y')),
    w: N(r.getAttribute('width')), h: N(r.getAttribute('height')) });
}
for (const t of doc.querySelectorAll('text.noteText')) {
  out.notes.push({ text: t.textContent, tx: N(t.getAttribute('x')), ty: N(t.getAttribute('y')) });
}
for (const ln of doc.querySelectorAll('line.loopLine')) {
  out.loops.push({ loopLine: true, x1: N(ln.getAttribute('x1')), y1: N(ln.getAttribute('y1')),
    x2: N(ln.getAttribute('x2')), y2: N(ln.getAttribute('y2')) });
}
for (const p of doc.querySelectorAll('polygon.labelBox')) {
  out.loops.push({ labelBox: p.getAttribute('points') });
}
for (const t of doc.querySelectorAll('text.labelText')) {
  out.loops.push({ labelText: t.textContent, x: N(t.getAttribute('x')), y: N(t.getAttribute('y')) });
}
for (const t of doc.querySelectorAll('text.loopText')) {
  out.loops.push({ loopText: t.textContent, x: N(t.getAttribute('x')), y: N(t.getAttribute('y')) });
}
for (const t of doc.querySelectorAll('text.sequenceNumber')) {
  out.numbers.push({ n: t.textContent, x: N(t.getAttribute('x')), y: N(t.getAttribute('y')) });
}
for (const r of doc.querySelectorAll('rect.rect')) {
  out.backgrounds.push({ x: N(r.getAttribute('x')), y: N(r.getAttribute('y')),
    w: N(r.getAttribute('width')), h: N(r.getAttribute('height')), fill: r.getAttribute('fill') });
}
writeFileSync(outPath, JSON.stringify(out, null, 2));
console.log('[extract] actors', out.actors.length, 'stickmen', out.stickmen.length,
  'msgs', out.messages.length, 'notes', out.notes.length, 'loops', out.loops.length,
  'numbers', out.numbers.length, 'canvas', out.canvas.width + 'x' + out.canvas.height);
