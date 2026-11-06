// Renders the app icon SVG into the PNG sizes the macOS icns needs.
//
// The icns container wants the icon at 16, 32, 64, 128, 256, 512 and
// 1024 pixels. The sizes come from the same SVG the Windows and the
// Store icons are rendered from, so all three platforms show the same
// art.
//
// Run: node tools/macos-icons/render.mjs

// The renderer is installed by the MSIX icon tool, which already
// depends on it.
import { Resvg } from '../msix-icons/node_modules/@resvg/resvg-js/index.js';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const root = path.resolve(here, '..', '..');
const svgPath = path.join(root, 'assets', 'icons', 'app-markdownit.svg');
const outDir = path.join(here, 'out');

fs.mkdirSync(outDir, { recursive: true });

const svg = fs.readFileSync(svgPath, 'utf8');

const sizes = [16, 32, 64, 128, 256, 512, 1024];

for (const size of sizes) {
  // The source SVG carries a fixed width, so the markup is rewritten
  // for the size that is rendered.
  const markup = svg
    .replace(/width="256"/, `width="${size}"`)
    .replace(/height="256"/, `height="${size}"`);

  const resvg = new Resvg(markup, {
    fitTo: { mode: 'width', value: size },
    background: 'rgba(0,0,0,0)',
  });
  const png = resvg.render().asPng();
  const out = path.join(outDir, `icon-${size}.png`);
  fs.writeFileSync(out, png);
  console.log(`${out} (${png.length} bytes)`);
}
