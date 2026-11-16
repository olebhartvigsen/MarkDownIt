// Renders the document (.md file) icon for the MSIX file type association
// Logo, from assets/icons/doc-markdown.svg: the same qualified targetsize
// set Square44x44Logo uses, since Explorer's "Open with" and file list read
// those variants for a FileTypeAssociation Logo too. finalize_doc.py
// downscales these 4x masters to the final sizes.
import { Resvg } from '@resvg/resvg-js';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const root = path.resolve(here, '..', '..');
const svgPath = path.join(root, 'assets', 'icons', 'doc-markdown.svg');
const outDir = path.join(here, 'out-4x-doc');
fs.mkdirSync(outDir, { recursive: true });

const svg = fs.readFileSync(svgPath, 'utf8');

function render4x(name, size) {
  const W = size * 4, H = size * 4;
  const markup = svg
    .replace(/width="256"/, `width="${W}"`)
    .replace(/height="256"/, `height="${H}"`);
  const resvg = new Resvg(markup, { background: 'rgba(0,0,0,0)' });
  fs.writeFileSync(path.join(outDir, `${name}.png`), resvg.render().asPng());
}

for (const t of [16, 20, 24, 30, 32, 36, 40, 44, 48, 56, 60, 64, 72, 80, 96, 256]) {
  render4x(`DocLogo44.targetsize-${t}`, t);
}
render4x('DocLogo44', 44);

console.log('rendered doc logo 4x masters into ' + outDir);
