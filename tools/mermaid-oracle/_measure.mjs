import { drawnTextBBox } from './text_metrics.mjs';
const t = 'Klikket opretter IKKE en person, kun en sag hos HR';
const d = drawnTextBBox(t, { fontFamily: '"Segoe UI", Arial, sans-serif', fontSize: 16 });
console.log(t, '->', d.width);
