// Instrument boundMessage + newActivation + activeEnd in a copy of the
// sequenceDiagram chunk so we can see EXACTLY what verticalPos is at each
// activation open/close. Usage: node instrument_seq.mjs
import fs from 'fs';
import { execSync } from 'child_process';

const src = '/workspace/MarkDownIt/tools/mermaid-oracle/node_modules/mermaid/dist/chunks/mermaid.esm.min/sequenceDiagram-NDJCBIXI.mjs';
let s = fs.readFileSync(src, 'utf8');

// 1. newActivation: log starty + actor
s = s.replace(
  'starty:this.verticalPos+2',
  'starty:(console.log("ACTSTART actor="+(e&&e.from||e&&e.to&&e.to.actor), "vP="+this.verticalPos, "starty="+(this.verticalPos+2)), this.verticalPos+2)'
);

// 2. activeEnd q: log before/after clamp
s = s.replace(
  'function q(m,_){let j=x.endActivation(m);j.starty+18>_&&(j.starty=_-6,_+=12)',
  'function q(m,_){console.log("ACTEND actor="+m.from,"vP="+_);let j=x.endActivation(m);j.starty+18>_&&(console.log("  CLAMP old_starty="+j.starty,"-> new vP="+(_+12)),j.starty=_-6,_+=12)'
);

const out = '/tmp/seq-instrumented.mjs';
fs.writeFileSync(out, s);

// 3. Run the oracle render on seq8 with the instrumented chunk.
// The oracle imports mermaid from node_modules; we temporarily replace the
// chunk in a copy of node_modules? Simpler: monkeypatch via require cache is
// hard in ESM. Instead: copy mermaid dir, swap chunk, run sequence_oracle.
const mm = '/workspace/MarkDownIt/tools/mermaid-oracle/node_modules/mermaid/dist/chunks/mermaid.esm.min';
const mm2 = '/tmp/mermaid-instr/chunks/mermaid.esm.min';
execSync(`rm -rf /tmp/mermaid-instr && mkdir -p ${mm2} && cp -r ${mm}/* ${mm2}/`);
fs.copyFileSync(out, mm2 + '/sequenceDiagram-NDJCBIXI.mjs');
console.log('instrumented chunk written');

// Run oracle with an env var pointing mermaid at the instrumented copy:
// mermaid's import resolution uses the local node_modules; we temporarily
// move ours aside. Check how sequence_oracle imports mermaid.
const oracle = fs.readFileSync('/workspace/MarkDownIt/tools/mermaid-oracle/sequence_oracle.mjs', 'utf8');
const m = oracle.match(/(import[^\n]*mermaid[^\n]*)/);
console.log('oracle import:', m && m[1]);