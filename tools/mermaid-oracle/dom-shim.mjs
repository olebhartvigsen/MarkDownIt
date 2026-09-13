// THE FIX DIRECTION: dompurify's default instance caches isSupported=false at
// first import. mermaid imports 'dompurify' → CJS with the broken default.
// Solution: pre-import dompurify ONLY AFTER globals exist, from within the
// same module graph order mermaid uses. But ESM import hoisting in dom-shim
// put `import DP from 'dompurify'` before the globalThis assignments!
// That is the whole bug: dom-shim.mjs's imports evaluate before its body.
// Order control: split into shim-a.mjs (globals only) + shim-b.mjs (DP bind).
// Simpler: do everything synchronously via createRequire inside dom-shim —
// require() is NOT hoisted.
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
  if (typeof process !== 'undefined' && process.env.BBOX_LOG) console.log('[bbox]', this.getAttribute && this.getAttribute('class'), JSON.stringify(t.length), JSON.stringify(t.slice(0,40)));
  return { x: -(t.length * 4) / 2, y: -6, width: t.length * 4, height: 12 };

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
