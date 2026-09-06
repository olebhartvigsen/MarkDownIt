import fs from 'node:fs';
import path from 'node:path';
import * as graphlib from 'dagre-d3-es/src/graphlib/index.js';
import { layout } from 'dagre-d3-es/src/dagre/index.js';

const NODE_SPACING = 50, RANK_SPACING = 50, EDGE_SEP = 10, PADDING = 15;
const CHAR_W = 8.4, LINE_H = 19;
const sizeFor = (label) => ({
  width:  Math.max(label.length * CHAR_W, 14) + PADDING * 2,
  height: LINE_H + PADDING * 2,
});

function parseFlow(src) {
  const lines = src.split('\n').map(s => s.trim()).filter(Boolean);
  const header = lines.shift();
  const dir = (header.match(/^(?:flowchart|graph)\s+(TD|TB|LR|RL|BT)/) || [, 'TB'])[1];
  const nodes = new Map(), edges = [];
  const nodeRe = /([A-Za-z0-9_]+)(?:\[([^\]]*)\]|\(\(([^)]*)\)\)|\(\[([^\]]*)\]\)|\(([^)]*)\)|\{([^}]*)\})?/g;
  for (const line of lines) {
    const m = line.match(/^(.*?)\s*(-{2,3}>|-\.-+>|={2,}>)\s*(?:\|([^|]*)\|)?\s*(.*)$/);
    const decl = (tok) => {
      nodeRe.lastIndex = 0;
      const g = nodeRe.exec(tok);
      if (!g) return null;
      const id = g[1];
      const label = g[2] ?? g[3] ?? g[4] ?? g[5] ?? g[6] ?? id;
      if (!nodes.has(id)) nodes.set(id, { id, label });
      return id;
    };
    if (m) {
      const from = decl(m[1]), to = decl(m[4]);
      edges.push({ from, to, label: m[3] || '' });
    } else {
      decl(line);
    }
  }
  return { dir, nodes: [...nodes.values()], edges };
}

// Compute integer ranks from node coordinates. For TB/BT layouts, rank
// tracks y; for LR/RL it tracks x. Dagre exposes node.rank only when the
// full simplex phase is preserved, so we recover it from geometry: group
// nodes by their axis coordinate and assign 0, 1, 2, ... in ascending
// order. This gives the same integer ranks the ranker computed.
function computeRanks(nodeObjs, rankdir) {
  const key = (rankdir === 'LR' || rankdir === 'RL') ? 'x' : 'y';
  const uniq = [...new Set(nodeObjs.map(n => n[key]))].sort((a, b) => a - b);
  return nodeObjs.map(n => uniq.indexOf(n[key]));
}

const file = process.argv[2];
const { dir, nodes, edges } = parseFlow(fs.readFileSync(file, 'utf8'));

const g = new graphlib.Graph({ multigraph: true, compound: true });
g.setGraph({ rankdir: dir, nodesep: NODE_SPACING, ranksep: RANK_SPACING, edgesep: EDGE_SEP, marginx: 0, marginy: 0 });
g.setDefaultEdgeLabel(() => ({}));
for (const n of nodes) g.setNode(n.id, { ...sizeFor(n.label), label: n.label });
for (const e of edges) g.setEdge(e.from, e.to, { weight: 1, minlen: 1, labelpos: 'c', label: e.label });

layout(g);

const nodeObjs = g.nodes().map(id => g.node(id));
const ranks = computeRanks(nodeObjs, dir);

const out = {
  source: path.basename(file),
  config: { rankdir: dir, nodesep: NODE_SPACING, ranksep: RANK_SPACING, edgesep: EDGE_SEP, padding: PADDING },
  graph: { width: g.graph().width, height: g.graph().height },
  nodes: g.nodes().map((id, i) => {
    const n = g.node(id);
    return { id, label: n.label, x: n.x, y: n.y, width: n.width, height: n.height, rank: ranks[i] };
  }),
  edges: g.edges().map(e => {
    const d = g.edge(e);
    return { from: e.v, to: e.w, label: d.label ?? '', points: (d.points || []).map(p => ({ x: p.x, y: p.y })) };
  }),
};
console.log(JSON.stringify(out, null, 2));
