// THE FIX DIRECTION: dompurify's default instance caches isSupported=false at
// first import. mermaid imports 'dompurify' → CJS with the broken default.
// Solution: pre-import dompurify ONLY AFTER globals exist, from within the
// same module graph order mermaid uses. But ESM import hoisting in dom-shim
// put `import DP from 'dompurify'` before the globalThis assignments!
// That is the whole bug: dom-shim.mjs's imports evaluate before its body.
// Order control: split into shim-a.mjs (globals only) + shim-b.mjs (DP bind).
// Simpler: do everything synchronously via createRequire inside dom-shim —
// require() is NOT hoisted.
const { calculateTextDims, drawnTextBBox, veneeredTextBBox } =
  await import('./text_metrics.mjs');
globalThis.__TEXT_METRICS = { calculateTextDims, drawnTextBBox, veneeredTextBBox };
import { JSDOM } from 'jsdom';

const w = new JSDOM('<!DOCTYPE html><body></body>').window;
globalThis.window = w;
globalThis.document = w.document;
const { Element, Node, DocumentFragment, DOMParser, CSS, SVGElement } = w;
globalThis.Element = Element;
globalThis.Node = Node;
globalThis.DocumentFragment = DocumentFragment;
globalThis.DOMParser = DOMParser;
if (CSS) globalThis.CSS = CSS;
if (SVGElement) globalThis.SVGElement = SVGElement;
Object.defineProperty(globalThis, 'navigator', { value: w.navigator, configurable: true });

const { Element: Pel } = w;
Pel.prototype.getBBox = function () {
  // Exclude <style> CSS text from measurements: svg-root textContent
  // includes the stylesheet, which exploded the viewBox width. Clone
  // without style/defs children and measure that.
  const tag = this.tagName ? this.tagName.toLowerCase() : '';
  if (tag === 'style' || tag === 'defs') {
    return { x: 0, y: 0, width: 0, height: 0 };
  }
  let t = '';
  const paths = [];
  const walk = (node) => {
    for (const child of node.childNodes || []) {
      const ctag = child.tagName ? child.tagName.toLowerCase() : '';
      if (ctag === 'style' || ctag === 'defs') continue;
      if (child.nodeType === 3) t += String(child.textContent ?? '');
      else if (ctag === 'path') paths.push(child);
      else walk(child);
    }
  };
  walk(this);
  if (!t && paths.length) {
    // No text: measure geometric bounds from path d attributes instead of
    // returning the empty-text 12px-tall box. Needed by classBox's
    // updateNodeBounds: the label-container holds the class box path, and
    // dagre sizes/positions derive from it.
    let minx = Infinity, miny = Infinity, maxx = -Infinity, maxy = -Infinity;
    for (const p of paths) {
      const nums = String(p.getAttribute('d') ?? '').match(/-?[\d.]+/g) ?? [];
      for (let i = 0; i + 1 < nums.length; i += 2) {
        const x = Number(nums[i]), y = Number(nums[i + 1]);
        if (x < minx) minx = x;
        if (x > maxx) maxx = x;
        if (y < miny) miny = y;
        if (y > maxy) maxy = y;
      }
    }
    if (minx !== Infinity) {
      return { x: minx, y: miny, width: maxx - minx, height: maxy - miny };
    }
  }
  if (!t) t = String(this.textContent ?? '');
  if (!t) return { x: 0, y: 0, width: 0, height: 0 };
  // Real font metrics only when the caller opts in (sequence oracle sets
  // __ORACLE_REAL_FONTS). Other diagram oracles (class/pie/flowchart) keep
  // their historical Len*4 calibration the C++ engines were tuned against.
  if (!globalThis.__ORACLE_REAL_FONTS) {
    return { x: -(t.length * 4) / 2, y: -6, width: t.length * 4, height: 12 };
  }
  // getBBox of a DRAWN <text> (advances sum, ink-ish
  // height ≈ round(size*1.06); the size comes from the element's own
  // style (config-mapped by mermaid: 16px on most sequence elements, or
  // the SVG root default).
  const style = this.getAttribute && this.getAttribute('style') || '';
  // Bitmap: mermaid measures mock texts (calculateTextDimensions) with a
  // plain construction -> line-box height round(size*1.33). The DRAWN
  // note/message texts carry dy=1em + dominant-baseline middle
  // (drawText3 valign center); Chrome gives those the ink box
  // round(size*1.06) (16px -> 17). drawNote sizes the note rect from the
  // drawn box, so only those shrink.
  const hasDy = this.getAttribute && this.getAttribute('dy') === '1em';
  const dominant = this.getAttribute && (this.getAttribute('dominant-baseline') ||
    this.getAttribute('alignment-baseline') || '');
  const veneered = hasDy && dominant === 'middle';
  let size = 16;
  const m = /font-size\s*:\s*([\d.]+)px/.exec(style);
  if (m) size = parseFloat(m[1]);
  else if (this.getAttribute('font-size')) {
    size = parseFloat(this.getAttribute('font-size')) || 16;
  }
  if (this.style && this.style.fontFamily) {
    // take both family AND size from live CSSOM (mermaid sets style first)
    this.__mermaidFontFamily = this.style.fontFamily;
    const fs = parseFloat(this.style.fontSize);
    if (fs) size = fs;
  }
  const fam = this.__mermaidFontFamily || 'sans-serif';
  const bb = (veneered ? __TEXT_METRICS.veneeredTextBBox : __TEXT_METRICS.drawnTextBBox)(t, { fontFamily: fam, fontSize: size });
  if (process.env.SHIM_TRACE_BB) console.error('[bb]', JSON.stringify({ t: t.slice(0, 24), fam, size, bb }));
  return bb;
};
Pel.prototype.getCTM = () => ({ a: 1, b: 0, c: 0, d: 1, e: 0, f: 0 });
Pel.prototype.getBoundingClientRect = function () {
  const t = String(this.textContent ?? '');
  const n = t.length;
  return { x: 0, y: 0, width: n * 8, height: 20, top: 0, left: 0,
    bottom: 20, right: n * 8 };
};

// NOW require dompurify — the default instance sees a real window.
const { createRequire } = await import('node:module');
const req = createRequire(import.meta.url);
const DP = req('dompurify');
console.log('[shim] DP.isSupported after globals:', DP.isSupported, 'sanitize:', typeof DP.sanitize);
globalThis.DOMPurify = DP;
