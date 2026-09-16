import { renderDiagram } from './oracle_util.mjs';
import { readFileSync, writeFileSync } from 'node:fs';
const src = readFileSync(process.argv[2], 'utf8');
const { svg } = await renderDiagram('sequence', src, {});
writeFileSync(process.argv[3], svg);
console.log('wrote', process.argv[3]);
