// Text metric bridge for the mermaid oracle shim. Replaces the old
// "utf16 units * 4 wide, 12 tall" getBBox lie with real font metrics.
//
// The user's real Chrome render (verified export) measures through the
// mermaid calculateTextDimensions pipeline:
//   - It measures the text twice: once for plain "sans-serif" (Windows
//     Chrome resolves that to Segoe UI), once for the configured family
//     stack ("trebuchet ms", "Open Sans", ...). It returns the sans-serif
//     set only if it wins on width AND height AND lineHeight.
//   - Measured one-line height (layout box) = round((asc - desc + gap) /
//     upm * size); width = advance sum, Math.round applied by mermaid.
//   - Drawn-element getBBox (notes, actor labels) uses the drawn text's
//     box: one line at 16px = 17 high in the verified export.
//
// Font resolution (Windows Chrome behavior, matching the user render):
//   sans-serif  → Segoe UI (from /mnt/c on this WSL; CI falls back)
//   trebuchet ms / verdana / arial → those Windows fonts when present
//   open sans   → vendored OFL copy in fonts/
// One metric engine, TTF-parsed, no native deps, CI-safe.
import { readFileSync, existsSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

const HERE = dirname(fileURLToPath(import.meta.url));
const FONT_DIR = join(HERE, 'fonts');

// DETERMINISTIC font stack: only the vendored OFL Open Sans, identical on
// WSL and CI. The /mnt/c Segoe lookup made goldens machine-dependent.
const FONT_FILES = {
  'segoe ui': [join(FONT_DIR, 'OpenSans-Regular.ttf')],
  'sans-serif': [join(FONT_DIR, 'OpenSans-Regular.ttf')],
  'arial': [join(FONT_DIR, 'OpenSans-Regular.ttf')],
  'trebuchet ms': [join(FONT_DIR, 'OpenSans-Regular.ttf')],
  'verdana': [join(FONT_DIR, 'OpenSans-Regular.ttf')],
  'open sans': [join(FONT_DIR, 'OpenSans-Regular.ttf')],
};
const DEFAULT_STACK = [join(FONT_DIR, 'OpenSans-Regular.ttf')];

function parseTtf(buf) {
  const u = {
    readU16: (o) => buf.readUInt16BE(o),
    readI16: (o) => buf.readInt16BE(o),
    readU32: (o) => buf.readUInt32BE(o),
  };
  const tables = {};
  const numTables = buf.readUInt16BE(4);
  for (let i = 0; i < numTables; i++) {
    const off = 12 + i * 16;
    const tag = buf.toString('latin1', off, off + 4);
    tables[tag] = { offset: buf.readUInt32BE(off + 8), length: buf.readUInt32BE(off + 12) };
  }
  return { buf, ...u, tables };
}

function loadFontFile(path) {
  const ttf = parseTtf(readFileSync(path));
  const upm = ttf.readU16(ttf.tables['head'].offset + 18);
  const numH = ttf.readU16(ttf.tables['hhea'].offset + 34);
  const hhea = ttf.tables['hhea'].offset;
  const asc = ttf.readI16(hhea + 4);
  const desc = ttf.readI16(hhea + 6);
  const gap = ttf.readI16(hhea + 8);
  const hmtx = ttf.tables['hmtx'].offset;

  // cmap: pick the format 4 (3,1) or format 0 subtable
  const cmap = ttf.tables['cmap'].offset;
  const nSub = ttf.readU16(cmap + 2);
  let sub = null, fmt = 0;
  for (let i = 0; i < nSub; i++) {
    const off = cmap + 4 + i * 8;
    const pid = ttf.readU16(off), eid = ttf.readU16(off + 2);
    const so = ttf.readU32(off + 4);
    if ((pid === 3 && (eid === 1 || eid === 0)) || pid === 0) {
      const f = ttf.readU16(cmap + so);
      if (f === 4 || f === 0) { sub = cmap + so; fmt = f; if (f === 4) break; }
    }
  }
  if (sub === null) return null;

  const font = {
    upm, numH, asc, desc, gap, sub, fmt, ttf,
    glyphCache: new Map(),
    glyphCacheP4: null,
    advanceCache: new Map(),
  };
  if (fmt === 4) {
    const segX2 = ttf.readU16(sub + 6);
    const seg = segX2 / 2;
    const endO = sub + 14;
    const startO = endO + segX2 + 2;
    const deltaO = startO + segX2;
    const rangeO = deltaO + segX2;
    font.p4 = { seg, endO, startO, deltaO, rangeO };
  }
  return font;
}

function glyphOf(font, code) {
  const cached = font.glyphCache.get(code);
  if (cached !== undefined) return cached;
  let g = 0;
  if (font.fmt === 4) {
    const { seg, endO, startO, deltaO, rangeO } = font.p4;
    const c = code & 0xFFFF;
    for (let s = 0; s < seg; s++) {
      const end = font.ttf.readU16(endO + s * 2);
      if (c <= end) {
        const start = font.ttf.readU16(startO + s * 2);
        if (c >= start && c !== 0xFFFF) {
          const delta = font.ttf.readI16(deltaO + s * 2);
          const range = font.ttf.readU16(rangeO + s * 2);
          if (range === 0) {
            g = (c + delta) & 0xFFFF;
          } else {
            const addr = rangeO + s * 2 + range + (c - start) * 2;
            const raw = font.ttf.readU16(addr);
            g = raw === 0 ? 0 : (raw + delta) & 0xFFFF;
          }
        }
        break;
      }
    }
  } else if (font.fmt === 0) {
    g = code < 256 ? font.ttf.buf[font.sub + 6 + code] : 0;
  }
  font.glyphCache.set(code, g);
  return g;
}

function advanceOf(font, code) {
  const g = glyphOf(font, code);
  let a = font.advanceCache.get(g);
  if (a !== undefined) return a;
  a = g >= font.numH
    ? font.ttf.readU16(font.ttf.tables['hmtx'].offset + (font.numH - 1) * 4)
    : font.ttf.readU16(font.ttf.tables['hmtx'].offset + g * 4);
  font.advanceCache.set(g, a);
  return a;
}

function widthOf(font, text, size) {
  let total = 0;
  for (const ch of text) total += advanceOf(font, ch.codePointAt(0));
  return total * Math.abs(size) / font.upm;
}

function lineHeightOf(font, size) {
  return (font.asc - font.desc + font.gap) / font.upm * size;
}

const FAMILY_CACHE = new Map();
function resolveFamilyList(familyStr) {
  const families = String(familyStr ?? '')
    .split(',')
    .map((s) => s.trim().replace(/^["']|["']$/g, '').toLowerCase())
    .filter(Boolean);
  if (!families.length) families.push('sans-serif');
  for (const fam of families) {
    if (FAMILY_CACHE.has(fam)) return FAMILY_CACHE.get(fam);
    const f = loadFontFile((FONT_FILES[fam] ?? DEFAULT_STACK).find((p) => existsSync(p)) ?? '');
    if (f) { FAMILY_CACHE.set(fam, f); return f; }
  }
  {
    let f = FAMILY_CACHE.get('sans-serif');
    if (!f) {
      f = loadFontFile(DEFAULT_STACK.find((p) => existsSync(p)) ?? '');
      FAMILY_CACHE.set('sans-serif', f);
    }
    return f;
  }
}

// ---- mermaid pipeline emulation -------------------------------------------

const LINE_BREAK = /\r?\n|\r(?!\n)|<br\s*\/?\s*>/i;

// calculateTextDimensions(text, config) emulation. config:
// { fontFamily, fontSize, fontWeight }.
export function calculateTextDims(text, { fontFamily = 'Arial', fontSize = 12 } = {}) {
  if (!text) return { width: 0, height: 0, lineHeight: 0 };
  const lines = String(text).split(LINE_BREAK);
  const primary = resolveFamilyList('sans-serif');
  const secondary = resolveFamilyList(fontFamily);
  const dims = (font) => {
    let w = 0, lh = 0;
    for (const line of lines) {
      w = Math.max(w, widthOf(font, line, fontSize));
      lh = Math.max(lh, lineHeightOf(font, fontSize));
    }
    return { width: Math.round(w), height: Math.round(lh * 1.332) * lines.length,
             lineHeight: Math.round(lh * 1.332) };
  };
  const d0 = dims(primary);
  const d1 = dims(secondary);
  const take0 = d0.height > d1.height && d0.width > d1.width &&
                d0.lineHeight > d1.lineHeight;
  return take0 ? d0 : d1;
}

// getBBox of a DRAWN <text> element (16px verified export: one line = 17
// high, width = advance sum unrounded).
export function drawnTextBBox(text, { fontFamily = 'Open Sans, sans-serif', fontSize = 16 } = {}) {
  const font = resolveFamilyList(fontFamily);
  const w = font ? widthOf(font, text || '', fontSize) : (text || '').length * 8;
  return { x: -w / 2, y: -(Math.round(fontSize * 1.33) / 2),
           width: w, height: Math.round(fontSize * 1.33) };
}
