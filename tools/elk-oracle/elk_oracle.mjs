// Reference geometry from Eclipse Layout Kernel itself.
//
// The layered port needs something honest to be measured against, and
// the only honest reference is ELK. This script takes a graph the app
// already built (nodes with their measured size, edges with their
// labels), hands it to elkjs with the settings the app uses, and writes
// the geometry in the same JSON shape as the dagre goldens.
//
// Usage: node elk_oracle.mjs graph.json out.json [TD|LR|BT|RL]
//
// The graph JSON comes from FlowchartToLayoutGraph, so node sizes are the
// app's own: a difference in the output is a difference in the layout,
// never in the measurement.

import fs from 'node:fs';
import ELK from 'elkjs/lib/elk.bundled.js';

const [graphPath, outPath, rankdir = 'TD'] = process.argv.slice(2);
if (!graphPath || !outPath) {
  console.error('usage: node elk_oracle.mjs graph.json out.json [TD|LR|BT|RL]');
  process.exit(2);
}

const direction = { TD: 'DOWN', BT: 'UP', LR: 'RIGHT', RL: 'LEFT' }[rankdir];
if (!direction) {
  console.error(`unknown rankdir ${rankdir}`);
  process.exit(2);
}

const graph = JSON.parse(fs.readFileSync(graphPath, 'utf8'));

// The settings the app lays out with, spelled the way ELK spells them.
const options = {
  'elk.algorithm': 'layered',
  'elk.direction': direction,
  'elk.spacing.nodeNode': '20',
  'elk.layered.spacing.nodeNodeBetweenLayers': '20',
  'elk.spacing.edgeNode': '10',
  'elk.spacing.edgeEdge': '10',
  'elk.padding': '[top=12,left=12,bottom=12,right=12]',
  'elk.layered.layering.strategy': 'NETWORK_SIMPLEX',
  'elk.layered.nodePlacement.strategy': 'BRANDES_KOEPF',
  'elk.layered.cycleBreaking.strategy': 'GREEDY',
  'elk.edgeRouting': 'ORTHOGONAL',
  'elk.layered.considerModelOrder.strategy': 'NODES_AND_EDGES',
};

const elkGraph = {
  id: 'root',
  layoutOptions: options,
  children: graph.nodes.map((n) => ({
    id: n.id,
    width: n.width,
    height: n.height,
  })),
  edges: graph.edges.map((e, i) => ({
    id: `e${i}`,
    sources: [e.from],
    targets: [e.to],
    labels: e.label ? [{ text: e.label, width: 10, height: 10 }] : [],
  })),
};

const elk = new ELK();

elk
  .layout(elkGraph)
  .then((laid) => {
    const nodes = laid.children.map((c) => ({
      id: c.id,
      // ELK gives the top-left corner; the engine and the goldens carry
      // centres.
      x: c.x + c.width / 2,
      y: c.y + c.height / 2,
      width: c.width,
      height: c.height,
      rank: c.y,
    }));

    const edges = laid.edges.map((e) => {
      const points = [];
      for (const section of e.sections || []) {
        points.push([section.startPoint.x, section.startPoint.y]);
        for (const bend of section.bendPoints || []) {
          points.push([bend.x, bend.y]);
        }
        points.push([section.endPoint.x, section.endPoint.y]);
      }
      return {
        from: e.sources[0],
        to: e.targets[0],
        label: '',
        points,
      };
    });

    const out = {
      source: graph.source || '',
      rankdir,
      nodesep: 20,
      ranksep: 20,
      edgesep: 10,
      padding: 12,
      width: laid.width,
      height: laid.height,
      nodes,
      edges,
    };
    fs.writeFileSync(outPath, JSON.stringify(out, null, 2) + '\n');
    console.log(
      `${outPath}: ${nodes.length} nodes, ${edges.length} edges, ` +
        `${laid.width.toFixed(1)}x${laid.height.toFixed(1)}`,
    );
  })
  .catch((err) => {
    console.error(err);
    process.exit(1);
  });
