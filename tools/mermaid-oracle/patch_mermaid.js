// Postinstall patch: apply the engine's intentional design deviation to the
// vendored mermaid dist used by the golden oracle.
//
// Deviation (agreed with the user, see seq_layout.cpp kAltElseExtraSpace):
// titled frame label rows (loop/opt/alt/par/critical/break start, NOT rect)
// and each else/and/option section row get +12 viewBox px of vertical air
// AFTER the label row, i.e. added to the final vertical bump that follows
// addLoopFn/addSectionToLoop.
//
// adjustLoopHeightForWrap structure:
//   bounds.bumpVerticalPos(preMargin);
//   heightAdjust = postMargin (+ wrap growth);
//   addLoopFn(msg);
//   bounds.bumpVerticalPos(heightAdjust);   <-- +12 lands here
//
// rect start must NOT get the air: rect is the only frame where
// postMargin === preMargin (both conf.boxMargin), so the runtime guard
// `preMargin === postMargin ? 0 : 12` skips it.
//
// npm's ESM entry (exports["."].import) is dist/mermaid.core.mjs, whose
// sequence renderer lives in chunks/mermaid.core/sequenceDiagram-*.mjs.
// dist/mermaid.js (the big UMD bundle) is patched too for robustness.

import { readFileSync, writeFileSync, readdirSync } from 'node:fs';

const MARKER = 'KALT_ELSE_EXTRA_SPACE';
const EXTRA = '12';

function patchFile(path, finalBump) {
  let src = readFileSync(path, 'utf8');
  const wasPatched = src.includes(MARKER);
  if (wasPatched) return true;  // idempotent: prior patch survives
  if (!src.includes(finalBump)) {
    throw new Error(`patch_mermaid: bump site not found in ${path}`);
  }
  const patched = finalBump.replace(
    'bumpVerticalPos(heightAdjust)',
    `bumpVerticalPos(heightAdjust + (preMargin === postMargin ? 0 : ${EXTRA})) /* ${MARKER} */`,
  );
  writeFileSync(path, src.replace(finalBump, patched));
  return wasPatched;
}

// Footer strip: wrap the drawActors(footer) final bump so the strip is
// reserved like in a real browser (jsdom returns height 0 for the rects).
const FOOTER_MARKER = 'KSEQ_FOOTER_STRIP';
function patchFooterBump(path) {
  let src = readFileSync(path, 'utf8');
  if (src.includes(FOOTER_MARKER)) return true;
  // UMD bundle spells it conf2; the ESM chunks use conf.
  const confNames = ['conf2.boxMargin', 'conf.boxMargin'];
  for (const cn of confNames) {
    const site = 'bumpVerticalPos(maxHeight + ' + cn + ')';
    if (!src.includes(site)) continue;
    const replaced = src.replace(
      site,
      'bumpVerticalPos(Math.max(maxHeight, 65) + ' + cn + ') /* ' + FOOTER_MARKER + ' */',
    );
    writeFileSync(path, replaced);
    return false;
  }
  throw new Error(`patch_mermaid: footer bump site not found in ${path}`);
}

const ROOT = new URL('./node_modules/mermaid/dist/', import.meta.url).pathname;

// 1) The UMD bundle.
let already = patchFile(
  ROOT + 'mermaid.js',
  'addLoopFn(msg);\n    bounds.bumpVerticalPos(heightAdjust);',
);

// 2) The ESM core chunk actually resolved via exports["."].import.
const coreDir = ROOT + 'chunks/mermaid.core/';
for (const f of readdirSync(coreDir)) {
  if (f.startsWith('sequenceDiagram-') && f.endsWith('.mjs')) {
    const was = patchFile(
      coreDir + f,
      'addLoopFn(msg);\n  bounds.bumpVerticalPos(heightAdjust);',
    );
    already = already || was;
  }
}

let footerTouched = patchFooterBump(ROOT + 'mermaid.js');
for (const f of readdirSync(coreDir)) {
  if (f.startsWith('sequenceDiagram-') && f.endsWith('.mjs')) {
    footerTouched = patchFooterBump(coreDir + f) || footerTouched;
  }
}

console.log((already || footerTouched)
  ? '[patch_mermaid] re-applied (was already patched)'
  : '[patch_mermaid] applied (+12 after titled frame/section rows, footer strip reserved)');
