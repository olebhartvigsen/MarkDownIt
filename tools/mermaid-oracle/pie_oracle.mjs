// Pie oracle: emit a golden JSON for a pie diagram from real mermaid.js.
// Usage: node pie_oracle.mjs <source-file> <out.json>
//  - parses the .mmd source with mermaid's pie parser (via full render)
//  - dumps arcs (angles), labels, percents, legend, canvas size.
// The golden schema mirrors LaidOutPie so the C++ test can compare 1:1:
//   { height, radius, cx, cy, width, title, arcs: [{start,end,label,pct,
//     label_x,label_y,color}], legend: [{label,x,y,value}] }
import * as mpkg from 'mermaid';
import { readFileSync, writeFileSync } from 'node:fs';

const mermaid = mpkg.default ?? mpkg;

const [, , srcPath, outPath] = process.argv;
if (!srcPath || !outPath) {
    console.error('usage: node pie_oracle.mjs <source> <out.json>');
    process.exit(2);
}
const src = readFileSync(srcPath, 'utf8');

const { svg } = await mermaid.render('pie_oracle', src);

const doc = new globalThis.window.DOMParser().parseFromString(svg, 'image/svg+xml');

const gold = { title: null, width: 0, height: 450, cx: 225, cy: 225, radius: 185, arcs: [], legend: [] };

// Robust pie-group discovery: the pie group is the PARENT <g> of
// circle.pieOuterCircle (mermaid nesting puts legends as siblings of the
// pie group, so svg > g > g wrongly hits a legend parent first).
const pieG = doc.querySelector('circle.pieOuterCircle')?.parentElement ?? null;
if (pieG) {
    const tr = pieG.getAttribute('transform') ?? '';
    const m = /translate\(([-\d.]+),([-\d.]+)\)/.exec(tr);
    if (m) { gold.cx = parseFloat(m[1]); gold.cy = parseFloat(m[2]); }
}
gold.radius = parseFloat((doc.querySelector('circle.pieOuterCircle')?.getAttribute('r') ?? '186')) - 1;
gold.width = parseFloat((doc.querySelector('svg').getAttribute('viewBox') ?? '0 0 0 0').split(' ')[2]);

const title = doc.querySelector('text.pieTitleText');
gold.title = title ? title.textContent : null;

let k = 0;
const arcs = [];
for (const p of pieG.querySelectorAll('path.pieCircle')) arcs.push({ d: p.getAttribute('d') ?? '', fill: p.getAttribute('fill') ?? '' });
// Slice text: label percent + transform (translate x,y).
const sliceTexts = [];
for (const t of pieG.querySelectorAll('text.slice')) {
    const tr = t.getAttribute('transform') ?? '';
    const m = /translate\(([-\d.e]+),([-\d.e]+)\)/.exec(tr);
    sliceTexts.push({ text: t.textContent, x: m ? parseFloat(m[1]) : 0, y: m ? parseFloat(m[2]) : 0 });
}
const legendRows = [];
for (const g of doc.querySelectorAll('g.legend')) {
    const tr = g.getAttribute('transform') ?? '';
    const m = /translate\(([-\d.]+),([-\d.]+)\)/.exec(tr);
    const rect = g.querySelector('rect');
    const text = g.querySelector('text');
    legendRows.push({
        x: m ? parseFloat(m[1]) : 0,
        y: m ? parseFloat(m[2]) : 0,
        label: text ? text.textContent : '',
        fill: rect ? rect.getAttribute('style') : '',
    });
}
void k;

// Extract per-arc start/end angle from the path d. Keep the RAW atan2 pair
// (no t1-normalization!) — the sweep is e - s within one arc only when both
// stay in the same atan2 branch; normalizing t1 independently would treat a
// wrapped start as >2π sweep. The absolute offset is unwrapped later
// against the previous arc (contiguity), which restores the true continuous
// angle domain (0..2π across all arcs).
function anglesFromPath(d) {
    // d: M0,-rA r,r 0 0 1 x,y L0,0Z (full arc path with two arc flags)
    const m = /M([-\d.]+),([-\d.]+)A([\d.]+),([\d.]+),([\d.]+),([\d.]+),([\d.]+),([-\d.]+),([-\d.]+)L([\d.]+),([\d.]+)Z/.exec(d);
    if (!m) return null;
    const x0 = parseFloat(m[1]), y0 = parseFloat(m[2]);
    const x1 = parseFloat(m[8]), y1 = parseFloat(m[9]);
    const t0 = Math.atan2(x0, -y0);
    let t1 = Math.atan2(x1, -y1);
    // Normalize ONLY within the pair: if t1 < t0 the arc crossed the atan2
    // branch cut at ±π (not necessarily the 2π origin); add 2π once.
    if (t1 < t0) t1 += 2 * Math.PI;
    return { start: t0, end: t1 };
}

let prevEndRef = 0.0;
arcs.forEach((a, i) => {
    const ang = anglesFromPath(a.d);
    const st = sliceTexts[i] ?? { text: '', x: 0, y: 0 };
    let s = ang ? ang.start : null;
    let e = ang ? ang.end : null;
    // unwrap against the RUNNING previous arc's unwrapped end (not the raw
    // re-derivation: raw atan2 values fold past ±π into the negative half).
    // First arc anchors at its raw value (d3.pie starts at angle 0).
    if (i > 0 && s !== null && e !== null) {
        const pe = prevEndRef;
        while (s - pe > Math.PI) { s -= 2 * Math.PI; e -= 2 * Math.PI; }
        while (s - pe < -Math.PI) { s += 2 * Math.PI; e += 2 * Math.PI; }
    }
    prevEndRef = e;
    arcs[i] = {
        index: i,
        start: s,
        end: e,
        pct: st.text,
        label_x: st.x + gold.cx,
        label_y: st.y + gold.cy,
        fill: a.fill,
    };
});
gold.arcs = arcs;
gold.legend = legendRows;

writeFileSync(outPath, JSON.stringify(gold, null, 1));
console.log('pie oracle wrote', outPath, 'arcs:', arcs.length);
