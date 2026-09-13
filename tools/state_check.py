#!/usr/bin/env python3
# Compare state_probe output against the stt goldens. Verifies nodes, edge
# labels and edge d-strings within the +-0.5 DIP tolerance.
import json, subprocess, sys

TOL = 0.5

def parse_probe(out):
    nodes, edges, labels = {}, {}, []
    for line in out.splitlines():
        p = line.split()
        if not p: continue
        if p and p[0] == 'N' and len(p) >= 8:
            nodes[p[1]] = {'kind': p[2], 'cx': float(p[3]), 'cy': float(p[4]),
                           'w': float(p[5]), 'h': float(p[6])}
        elif p and p[0] == 'E' and len(p) >= 4:
            edges[p[1] + ' ' + p[3]] = {'d': p[5]}
        elif p and p[0] == 'L' and len(p) >= 5:
            labels.append({'text': p[1], 'x': float(p[2]), 'y': float(p[3]),
                           'w': float(p[4]), 'h': float(p[5])})
    return nodes, edges, labels

def main():
    bad = 0
    for name in sys.argv[1:]:
        out = subprocess.run(['/tmp/state_probe', f'tests/mermaid/fixtures/{name}.mmd',
                              f'tests/mermaid/golden/{name}.json'],
                             capture_output=True, text=True, cwd='/workspace/MarkDownIt')
        gnodes, pedges, glabels = parse_probe(out.stdout)
        g = json.load(open(f'tests/mermaid/golden/{name}.json'))
        gd = {ge['id']: ge['d'] for ge in g['edges']}
        for gn in g['nodes']:
            key = gn['id']
            if key not in gnodes:
                print(f'{name}: MISSING node {key}'); bad += 1; continue
            p = gnodes[key]
            for f in ('cx', 'cy', 'w', 'h'):
                if abs(p[f] - gn[f]) > TOL:
                    print(f'{name}: node {key} {f} {p[f]:.3f} != golden {gn[f]:.3f}'); bad += 1
        # labels: golden center vs our top-left
        if len(glabels) != len(g['edge_labels']):
            print(f'{name}: label count {len(glabels)} != golden {len(g["edge_labels"])}')
            bad += 1
        for gl, pl in zip(g['edge_labels'], glabels):
            cx = pl['x'] + pl['w'] / 2.0
            cy = pl['y'] + pl['h'] / 2.0
            if abs(cx - gl['x']) > TOL or abs(cy - gl['y']) > TOL:
                print(f'{name}: label {gl["text"]} center ({cx:.3f},{cy:.3f}) != golden ({gl["x"]},{gl["y"]})')
                bad += 1
        # edges: golden edges keyed edge0..edgeN in order
        import re
        for i, (key, pe) in enumerate(pedges.items()):
            gid = f'edge{i}'
            if gid not in gd:
                continue
            gdstr = gd[gid]
            mt = [float(x) for x in re.findall(r'-?[\d.]+', pe['d'])]
            gt = [float(x) for x in re.findall(r'-?[\d.]+', gdstr)]
            if len(mt) != len(gt):
                print(f'{name}: edge{i} token count {len(mt)} != {len(gt)}')
                bad += 1
                continue
            for a, b in zip(mt, gt):
                if abs(a - b) > TOL:
                    print(f'{name}: edge{i} token {a:.3f} != {b:.3f} (d={pe["d"]})')
                    bad += 1
                    break
    print(f'bad={bad}')
    sys.exit(1 if bad else 0)

if __name__ == '__main__':
    main()