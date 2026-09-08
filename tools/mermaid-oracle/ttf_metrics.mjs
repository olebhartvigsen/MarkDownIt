// Minimal TTF text-metric extractor: cmap format 4 + hmtx + head + OS/2 hhea.
// Pure Node, no deps. Exposes measureTtf(text, fontSize) → advance width in px.
// Reads the font file once at import; used by dom-shim for realistic widths.
import { readFileSync } from 'node:fs';

function parseTtf(buf) {
  const u16 = (o) => buf.readUInt16BE(o);
  const i16 = (o) => buf.readInt16BE(o);
  const u32 = (o) => buf.readUInt32BE(o);
  let numTables = u16(4);
  let tables = {};
  for (let i = 0; i < numTables; i++) {
    const off = 12 + i * 16;
    const tag = buf.toString('latin1', off, off + 4);
    tables[tag] = { offset: u32(off + 8), length: u32(off + 12) };
  }
  const head = tables['head'];
  const unitsPerEm = u16(head.offset + 18);
  const hhea = tables['hhea'];
  const numberOfHMetrics = u16(hhea.offset + 34);
  const hmtx = tables['hmtx'];
  // cmap: choose format 4 (BMP) or format 0
  const cmap = tables['cmap'];
  const nSub = u16(cmap.offset + 2);
  let cmapSub = null;
  for (let i = 0; i < nSub; i++) {
    const off = cmap.offset + 4 + i * 8;
    const platformID = u16(off), encodingID = u16(off + 2);
    const subOffset = u32(off + 4);
    if ((platformID === 3 && (encodingID === 1 || encodingID === 0)) || platformID === 0) {
      cmapSub = cmap.offset + subOffset;
      if (u16(cmapSub) === 4) break;  // prefer format 4
    }
  }
  if (!cmapSub) throw new Error('no usable cmap subtable');
  const cmapFormat = u16(cmapSub);
  let glyphId = () => 0;
  if (cmapFormat === 4) {
    const segCountX2 = u16(cmapSub + 6);
    const segCount = segCountX2 / 2;
    const endCodesO = cmapSub + 14;
    const startCodesO = endCodesO + segCountX2 + 2;  // +2 reservedPad
    const idDeltaO = startCodesO + segCountX2;
    const idRangeOffsetO = idDeltaO + segCountX2;
    glyphId = (code) => {
      for (let s = 0; s < segCount; s++) {
        const end = u16(endCodesO + s * 2);
        if (code <= end) {
          const start = u16(startCodesO + s * 2);
          if (code < start) return 0;
          const rangeOffset = u16(idRangeOffsetO + s * 2);
          if (rangeOffset === 0) {
            return (code + i16(idDeltaO + s * 2)) & 0xFFFF;
          }
          // glyph index address arithmetic per spec
          const addr = idRangeOffsetO + s * 2 + rangeOffset + (code - start) * 2;
          const g = u16(addr);
          if (g === 0) return 0;
          return (g + i16(idDeltaO + s * 2)) & 0xFFFF;
        }
      }
      return 0;
    };
  } else if (cmapFormat === 0) {
    glyphId = (code) => (code < 256 ? buf[cmapSub + 6 + code] : 0);
  } else {
    throw new Error('cmap format ' + cmapFormat + ' unsupported');
  }
  const advance = (g) => {
    if (g >= numberOfHMetrics) {
      // advanceWidth of last metric
      return u16(hmtx.offset + (numberOfHMetrics - 1) * 4);
    }
    return u16(hmtx.offset + g * 4);
  };
  return { unitsPerEm, glyphId, advance, kernTable: tables['kern'] ?? null, buf, u16, i16, tables };
}

function kernPairs(ttf) {
  // Windows GDI-style kern (format 0 subtable) — smallish effect, but mermaid text is
  // measured WITHOUT kerning by getBBox? Browser text measurement DOES apply kerning?
  // Chrome's measureText applies kerning by default. Include format-0 kern if present.
  const out = {};
  const t = ttf.kernTable;
  if (!t) return out;
  const buf = ttf.buf, u16 = ttf.u16, i16 = ttf.i16;
  let off = t.offset;
  const version = u16(off);
  let nTables = u16(off + 2);
  if (version === 0) { /* standard kern */ }
  else { nTables = u16(off + 4); off += 4; }  // Apple AAT kern — rare on Windows fonts
  let o = off + 4;
  for (let i = 0; i < nTables && i < 4; i++) {
    const sub = o;
    const len = u16(sub + 2);
    const coverage = u16(sub + 4);
    const format = coverage >> 8;
    if (format === 0) {
      const nPairs = u16(sub + 6);
      for (let p = 0; p < nPairs; p++) {
        const e = sub + 14 + p * 6;
        out[(u16(e) << 16) | u16(e + 2)] = i16(e + 4);
      }
    }
    o += len;
  }
  return out;
}

let canonCache = null;
let cacheFile = null;
export function loadTtf(path) {
  if (canonCache && cacheFile === path) return canonCache;
  const ttf = parseTtf(readFileSync(path));
  canonCache = { ...ttf, kern: kernPairs(ttf) };
  cacheFile = path;
  return canonCache;
}

// measureText(text, size) — NO letter-spacing; kerning applied (Chrome default behavior).
export function measureTtf(ttf, text, size) {
  if (!text) return 0;
  const scale = size / ttf.unitsPerEm;
  let total = 0;
  let prevG = -1;
  for (const ch of text) {
    const code = ch.codePointAt(0);
    const g = ttf.glyphId(code);
    total += ttf.advance(g);
    if (prevG >= 0) {
      const k = ttf.kern[(prevG << 16) | g];
      if (k) total -= k;
    }
    prevG = g;
  }
  return Math.abs(total * Math.abs(scale));
}
