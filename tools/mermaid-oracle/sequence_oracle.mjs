// Sequence-diagram oracle: render one fixture headless via the dom-shim and
// dump the geometric facts the native renderer must reproduce, as JSON.
// Geometry convention matches the mermaid draw() code:
//   - actor box (top):  rect.actor.actor-top  → x, y, w, h + text x/y
//   - actor box (foot): rect.actor.actor-bottom (mirrorActors)
//   - lifeline:         line.actor-line → x, y1, y2
//   - message:          line.messageLine0/1 (solid/dotted) with x1,y1,x2,y2 +
//                       text.messageText (x,y) in draw order; head marker class
//   - self message:     path with d= "M .. C .." + its text
//   - note:             rect.note + text.noteText
//   - activation:       rect.activation*
//   - loop:             line.loopLine (rect border) + polygon.labelBox +
//                       text.labelText/loopText
//   - canvas:           svg width/height/viewBox (startx/starty = viewBox mins)
import { readFileSync, writeFileSync } from 'node:fs';
import { renderDiagram } from './oracle_util.mjs';

const srcPath = process.argv[2];
const outPath = process.argv[3];
if (!srcPath || !outPath) {
  console.error('usage: node sequence_oracle.mjs <fixture.mmd> <out.json>');
  process.exit(2);
}

const src = readFileSync(srcPath, 'utf8');
const { svg } = await renderDiagram('sequence', src, {});

const doc = new DOMParser().parseFromString(svg, 'image/svg+xml');
const svgEl = doc.querySelector('svg');
const viewBox = (svgEl.getAttribute('viewBox') ?? '').split(/\s+/).map(Number);
const out = {
  canvas: {
    width: Number(svgEl.getAttribute('width')),
    height: Number(svgEl.getAttribute('height')),
    startx: viewBox[0], starty: viewBox[1],
    vbwidth: viewBox[2], vbheight: viewBox[3],
  },
  actors: [], messages: [], notes: [], activations: [], loops: [], backgrounds: [],
};

// ---- actors + lifelines (top + bottom) -------------------------------------
for (const r of doc.querySelectorAll('rect.actor')) {
  const isBottom = r.classList.contains('actor-bottom');
  out.actors.push({
    bottom: isBottom,
    name: r.getAttribute('name'),
    x: Number(r.getAttribute('x')), y: Number(r.getAttribute('y')),
    w: Number(r.getAttribute('width')), h: Number(r.getAttribute('height')),
  });
}
for (const l of doc.querySelectorAll('line.actor-line')) {
  out.actors.push({
    kind: 'lifeline',
    x: Number(l.getAttribute('x1')), y1: Number(l.getAttribute('y1')),
    y2: Number(l.getAttribute('y2')),
  });
}

// ---- messages: zip messageText elements with their matched line -------------
// drawMessage appends text first, then the line (self: path). Lines and paths
// alternate in document order matching the text order, so pair i-th text with
// the i-th following line/path in source order.
const textEls = [...doc.querySelectorAll('text.messageText')];
const lineEls = [...doc.querySelectorAll('line.messageLine0, line.messageLine1')];
const pathEls = [...doc.querySelectorAll('path.messageLine0, path.messageLine1')];
// reorder: for each text, find the nearest next line in DOM order.
// Simpler robust rule: mermaid emits [text, line] per message sequentially.
// Build the ordered ink sequence from the svg body children.
const all = svgEl.children.length
  ? doc.querySelectorAll('text.messageText, line.messageLine0, line.messageLine1, path.messageLine0, path.messageLine1')
  : [];
let pendingText = null;
for (const el of all) {
  const tag = el.tagName;
  const isText = tag === 'text';
  const isLine = tag === 'line';
  const isPath = tag === 'path';
  const self = isPath && /messageLine[01]/.test(el.getAttribute('class') ?? '');
  if (isText) { pendingText = el; continue; }
  if (!isLine && !self) continue;
  if (!pendingText) continue;
  const msg = {
    text: pendingText.textContent,
    tx: Number(pendingText.getAttribute('x')),
    ty: Number(pendingText.getAttribute('y')),
  };
  if (isLine) {
    msg.line = {
      x1: Number(el.getAttribute('x1')), y1: Number(el.getAttribute('y1')),
      x2: Number(el.getAttribute('x2')), y2: Number(el.getAttribute('y2')),
      cls: el.getAttribute('class'),
      markerEnd: el.getAttribute('marker-end') ?? '',
      markerStart: el.getAttribute('marker-start') ?? '',
    };
  } else {
    msg.path = { d: el.getAttribute('d'), cls: el.getAttribute('class') };
  }
  out.messages.push(msg);
  pendingText = null;
}

// ---- notes ---------------------------------------------------------------
const noteTexts = [...doc.querySelectorAll('text.noteText')];
const noteRects = [...doc.querySelectorAll('rect.note')];
for (let i = 0; i < noteTexts.length; i++) {
  const t = noteTexts[i];
  const r = noteRects[i]; // drawNote emits rect then text per note
  out.notes.push({
    text: t.textContent,
    tx: Number(t.getAttribute('x')), ty: Number(t.getAttribute('y')),
    rx: Number(r.getAttribute('x')), ry: Number(r.getAttribute('y')),
    rw: Number(r.getAttribute('width')), rh: Number(r.getAttribute('height')),
  });
}

// ---- activations ----------------------------------------------------------
for (const r of doc.querySelectorAll('rect.activation0, rect.activation1, rect.activation2')) {
  out.activations.push({
    x: Number(r.getAttribute('x')), y: Number(r.getAttribute('y')),
    w: Number(r.getAttribute('width')), h: Number(r.getAttribute('height')),
    cls: r.getAttribute('class'),
  });
}

// ---- loops (loop/alt/par/opt/critical/break rectangles) -------------------
const loopLines = [...doc.querySelectorAll('line.loopLine')];
const labelBoxes = [...doc.querySelectorAll('polygon.labelBox')];
const labelTxts = [...doc.querySelectorAll('text.labelText')];
const loopTxts = [...doc.querySelectorAll('text.loopText')];
out.loops.push({ loopLineCount: loopLines.length });
const seen = new Set();
for (const line of loopLines) {
  const x1 = Number(line.getAttribute('x1'));
  const y1 = Number(line.getAttribute('y1'));
  const x2 = Number(line.getAttribute('x2'));
  const y2 = Number(line.getAttribute('y2'));
  const horiz = y1 === y2;
  if (horiz) {
    // top edge: startx,starty → stopx,starty ; bottom edge: startx,stopy → stopx,stopy
    out.loops.push({ top: horiz && true, x1, y1, x2, y2 });
  } else {
    out.loops.push({ vert: true, x1, y1, x2, y2 });
  }
  void seen;
}
for (const p of labelBoxes) out.loops.push({ labelBox: p.getAttribute('points') });
for (const t of labelTxts) out.loops.push({ labelText: t.textContent, x: Number(t.getAttribute('x')), y: Number(t.getAttribute('y')) });
for (const t of loopTxts) out.loops.push({ loopText: t.textContent, x: Number(t.getAttribute('x')), y: Number(t.getAttribute('y')) });
// section dividers are dashed loopLines; keep class info
out.loops = out.loops.filter(Boolean);

// ---- title ---------------------------------------------------------------
// Title: draw() appends text with y=-25, x = (stopx-startx)/2 - 2*marginX
// (exactCentered via classless baretext in jsdom). Keep class check permissive.
const titleEl = [...doc.querySelectorAll('text')].find(
  (t) => !t.getAttribute('class') && t.getAttribute('y') === '-25');
if (titleEl) {
  out.title = { text: titleEl.textContent, x: Number(titleEl.getAttribute('x')), y: Number(titleEl.getAttribute('y')) };
}

// ---- backgrounds (rect rgb(...) sources; class "rect") --------------------
for (const r of doc.querySelectorAll('rect.rect')) {
  out.backgrounds.push({
    x: Number(r.getAttribute('x')), y: Number(r.getAttribute('y')),
    w: Number(r.getAttribute('width')), h: Number(r.getAttribute('height')),
    fill: r.getAttribute('fill'),
  });
}

// autonumber circles
const seqNumbers = [...doc.querySelectorAll('text.sequenceNumber')];
if (seqNumbers.length) {
  out.sequenceNumbers = seqNumbers.map((s) => ({ n: s.textContent, x: Number(s.getAttribute('x')), y: Number(s.getAttribute('y')) }));
}

writeFileSync(outPath, JSON.stringify(out, null, 2));
console.log(`[seq] actors=${out.actors.length} msgs=${out.messages.length} notes=${out.notes.length} activations=${out.activations.length} canvas=${out.canvas.width}x${out.canvas.height}`);
