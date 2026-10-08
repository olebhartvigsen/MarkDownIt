// Renders 4x supersampling masters for every MSIX tile/logo asset from the
// app icon SVG. finalize.py downscales these to the final sizes in
// installer/msix/assets/. Run: npm install && npm run render
import { Resvg } from '@resvg/resvg-js';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const root = path.resolve(here, '..', '..');
const svgPath = path.join(root, 'assets', 'icons', 'app-markdownit.svg');
const outDir = path.join(here, 'out-4x');
fs.mkdirSync(outDir, { recursive: true });

const svg = fs.readFileSync(svgPath, 'utf8');

// Render one job at 4x its final size.
// fit 'square': full-bleed icon. fit 'center': icon centered on a transparent
// canvas (non-square tiles per the Store's icon quality rules).
function render4x(name, w, h, fit) {
  const W = w * 4, H = h * 4;
  let markup;
  if (fit === 'square') {
    markup = svg
      .replace(/width="256"/, `width="${W}"`)
      .replace(/height="256"/, `height="${H}"`);
  } else {
    const s = Math.min(W, H);
    const x = (W - s) / 2, y = (H - s) / 2;
    // Rebuild the nested root tag so it carries exactly one set of geometry
    // attributes (the source root keeps its own width/height otherwise).
    const inner = svg.replace(
      /<svg[^>]*>/,
      `<svg x="${x}" y="${y}" width="${s}" height="${s}" viewBox="0 0 256 256" xmlns="http://www.w3.org/2000/svg">`
    );
    markup = `<svg xmlns="http://www.w3.org/2000/svg" width="${W}" height="${H}" viewBox="0 0 ${W} ${H}">${inner}</svg>`;
  }
  const resvg = new Resvg(markup, { background: 'rgba(0,0,0,0)' });
  fs.writeFileSync(path.join(outDir, `${name}.png`), resvg.render().asPng());
}

const jobs = [{ name: 'StoreLogo', w: 50, h: 50, fit: 'square' }];

for (const [base, size] of [['Logo44', 44], ['Logo71', 71], ['Logo150', 150], ['Logo310', 310]]) {
  jobs.push({ name: base, w: size, h: size, fit: 'square' });
  for (const s of [100, 125, 150, 200, 400]) {
    jobs.push({ name: `${base}.scale-${s}`, w: Math.round(size * s / 100), h: Math.round(size * s / 100), fit: 'square' });
  }
}

// targetsize assets: the taskbar/Alt+Tab read qualified variants of
// Square44x44Logo; without the altform-unplated set the shell plates the
// icon with BackgroundColor.
for (const t of [16, 20, 24, 30, 32, 36, 40, 44, 48, 56, 60, 64, 72, 80, 96, 256]) {
  jobs.push({ name: `Logo44.targetsize-${t}`, w: t, h: t, fit: 'square' });
}

jobs.push({ name: 'Wide310x150', w: 310, h: 150, fit: 'center' });
jobs.push({ name: 'SplashScreen', w: 620, h: 300, fit: 'center' });

for (const j of jobs) render4x(j.name, j.w, j.h, j.fit);
console.log(`rendered ${jobs.length} 4x masters into ${outDir}`);