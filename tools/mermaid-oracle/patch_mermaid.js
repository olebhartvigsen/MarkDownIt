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

console.log(already
  ? '[patch_mermaid] re-applied (was already patched)'
  : '[patch_mermaid] applied (+12 after titled frame/section rows)');
