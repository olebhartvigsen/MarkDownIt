import fs from 'node:fs';
import path from 'node:path';
import * as graphlib from 'dagre-d3-es/src/graphlib/index.js';
import { layout } from 'dagre-d3-es/src/dagre/index.js';

const NODE_SPACING = 50, RANK_SPACING = 50, EDGE_SEP = 10, PADDING = 15;
const CHAR_W = 8.4, LINE_H = 19;
// Swimlane band paddings. Must match src/mermaid/swimlanes.cpp constants.
const LANE_PADDING = 20;
const LANE_TITLE_BAND = 20;
const sizeFor = (label) => ({
  width:  Math.max(label.length * CHAR_W, 14) + PADDING * 2,
  height: LINE_H + PADDING * 2,
});

function parseFlow(src) {
  const rawLines = src.split('\n').map(s => s.trim()).filter(Boolean);
  const header = rawLines.shift();
  const dir = (header.match(/^(?:flowchart|graph)\s+(TD|TB|LR|RL|BT)/) || [, 'TB'])[1];
  const nodes = new Map(), edges = [];
  // subgraphs: {id, title, direction?, nodeIds:Set, children:[]} plus stack-based parenting.
  const subgraphs = [];
  const stack = [];
  const nodeRe = /([A-Za-z0-9_]+)(?:\[([^\]]*)\]|\(\(([^)]*)\)\)|\(\[([^\]]*)\]\)|\(([^)]*)\)|\{([^}]*)\})?/g;
  const decl = (tok) => {
    nodeRe.lastIndex = 0;
    const g = nodeRe.exec(tok);
    if (!g) return null;
    const id = g[1];
    const label = g[2] ?? g[3] ?? g[4] ?? g[5] ?? g[6] ?? id;
    if (!nodes.has(id)) nodes.set(id, { id, label });
    for (const s of stack) subgraphs[s].nodeIds.add(id);
    return id;
  };
  for (const line of rawLines) {
    const sgm = line.match(/^subgraph\s+([A-Za-z_][A-Za-z0-9_]*)(?:\s*\[([^\]]*)\])?(?:\s+(.*))?$/);
    if (sgm) {
      const id = sgm[1];
      const title = sgm[2] || sgm[3] || id;
      const idx = subgraphs.length;
      subgraphs.push({ id, title, nodeIds: new Set(), children: [] });
      if (stack.length) subgraphs[stack[stack.length - 1]].children.push(idx);
      stack.push(idx);
      continue;
    }
    if (line === 'end' || line === 'END' || line === 'End') {
      stack.pop();
      continue;
    }
    const dm = line.match(/^direction\s+(TB|TD|BT|LR|RL)$/);
    if (dm && stack.length) {
      subgraphs[stack[stack.length - 1]].direction = dm[1] === 'TD' ? 'TB' : dm[1];
      continue;
    }
    // Form 1: inline label between dashes: `A -- text --> B` / `A -- text --- B`.
    const mi = line.match(/^(.+?)\s*--\s+([^->].*?)\s*--+>?\s*(.+)$/);
    if (mi) {
      const from = decl(mi[1]), to = decl(mi[3]);
      edges.push({ from, to, label: mi[2].trim(), head: 'arrow' });
      continue;
    }
    // Form 2: plain op with optional |label|: -->, ---, -.->, ==>, -. - .
    const m = line.match(/^(.*?)\s*(-{2,3}>?|-\.->|={2,}>|-\.+-)\s*(?:\|([^|]*)\|)?\s*(.*)$/);
    if (m) {
      const from = decl(m[1]), to = decl(m[4]);
      const head = m[2].endsWith('>') ? 'arrow' : 'none';
      edges.push({ from, to, label: m[3] || '', head });
    } else {
      decl(line);
    }
  }
  return { dir, nodes: [...nodes.values()], edges, subgraphs };
}

function computeRanks(nodeObjs, rankdir) {
  const key = (rankdir === 'LR' || rankdir === 'RL') ? 'x' : 'y';
  const uniq = [...new Set(nodeObjs.map(n => n[key]))].sort((a, b) => a - b);
  return nodeObjs.map(n => uniq.indexOf(n[key]));
}

// Same algorithm as src/mermaid/swimlanes.cpp: axis-aligned bbox of nodes
// plus LANE_PADDING on all sides plus LANE_TITLE_BAND above.
function laneBoxesFrom(subgraphs, nodesById) {
  const collect = (idx, set) => {
    for (const id of subgraphs[idx].nodeIds) set.add(id);
    for (const c of subgraphs[idx].children) collect(c, set);
  };
  const out = [];
  for (let si = 0; si < subgraphs.length; ++si) {
    const set = new Set();
    collect(si, set);
    let minx = Infinity, miny = Infinity, maxx = -Infinity, maxy = -Infinity, any = false;
    for (const id of set) {
      const n = nodesById.get(id);
      if (!n) continue;
      const l = n.x - n.width / 2, r = n.x + n.width / 2;
      const t = n.y - n.height / 2, b = n.y + n.height / 2;
      if (l < minx) minx = l; if (t < miny) miny = t;
      if (r > maxx) maxx = r; if (b > maxy) maxy = b;
      any = true;
    }
    if (!any) continue;
    out.push({
      id: subgraphs[si].id,
      title: subgraphs[si].title,
      x: minx - LANE_PADDING,
      y: miny - LANE_PADDING - LANE_TITLE_BAND,
      width:  (maxx - minx) + 2 * LANE_PADDING,
      height: (maxy - miny) + 2 * LANE_PADDING + LANE_TITLE_BAND,
      children: subgraphs[si].children.slice(),
    });
  }
  return out;
}

const file = process.argv[2];
const { dir, nodes, edges, subgraphs } = parseFlow(fs.readFileSync(file, 'utf8'));

const g = new graphlib.Graph({ multigraph: true, compound: true });
g.setGraph({ rankdir: dir, nodesep: NODE_SPACING, ranksep: RANK_SPACING, edgesep: EDGE_SEP, marginx: 0, marginy: 0 });
g.setDefaultEdgeLabel(() => ({}));
for (const n of nodes) g.setNode(n.id, { ...sizeFor(n.label), label: n.label });
for (const e of edges) g.setEdge(e.from, e.to, { weight: 1, minlen: 1, labelpos: 'c', label: e.label });

layout(g);
if (process.env.ORDER_DEBUG) {
  console.error('ORDERS:', g.nodes().map(id => id + '=' + (g.node(id).order ?? 'x')).join(' '));
}

const nodeObjs = g.nodes().map(id => g.node(id));
const ranks = computeRanks(nodeObjs, dir);
const nodesById = new Map();
for (const id of g.nodes()) nodesById.set(id, g.node(id));

const lanes = laneBoxesFrom(subgraphs, nodesById);

const out = {
  source: path.basename(file),
  config: { rankdir: dir, nodesep: NODE_SPACING, ranksep: RANK_SPACING, edgesep: EDGE_SEP, padding: PADDING, lanePadding: LANE_PADDING, laneTitleBand: LANE_TITLE_BAND },
  graph: { width: g.graph().width, height: g.graph().height },
  nodes: g.nodes().map((id, i) => {
    const n = g.node(id);
    return { id, label: n.label, x: n.x, y: n.y, width: n.width, height: n.height, rank: ranks[i] };
  }),
  edges: g.edges().map(e => {
    const d = g.edge(e);
    return { from: e.v, to: e.w, label: d.label ?? '', points: (d.points || []).map(p => ({ x: p.x, y: p.y })) };
  }),
  subgraphs: lanes,
};
console.log(JSON.stringify(out, null, 2));
