import { readFileSync } from 'node:fs';
import { renderDiagram } from './oracle_util.mjs';

const srcPath = process.argv[2];
const src = readFileSync(srcPath, 'utf8');
const { svg } = await renderDiagram('class', src, {});
// strip style/defs content the way the shim does, then count text
const cleaned = svg.replace(/<style[\s\S]*?<\/style>/g, '').replace(/<defs[\s\S]*?<\/defs>/g, '');
const texts = [...cleaned.matchAll(/>([^<>]+)</g)].map(m => m[1]).filter(t => t.trim());
console.log('SVG-ROOT text pieces:', JSON.stringify(texts));
const total = texts.join('').length;
console.log('TOTAL textlen =', total, '-> w =', total * 4 + 16, 'h = 28');
