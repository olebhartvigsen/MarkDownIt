import { readFileSync } from 'node:fs';
import { renderDiagram } from './oracle_util.mjs';

const src = readFileSync(process.argv[2], 'utf8');
const { svg } = await renderDiagram('class', src, {});
const m = svg.match(/<g class="edgeTerminals"[\s\S]*?<\/g><\/g>/g) ?? [];
for (const frag of m) console.log('FRAG:', frag);
// also edgeLabels groups
const m2 = svg.match(/<g class="edgeLabels">[\s\S]*?<\/g><\/g><\/g>/g) ?? [];
for (const frag of m2) console.log('EDGELABELS:', frag.slice(0, 2000));
