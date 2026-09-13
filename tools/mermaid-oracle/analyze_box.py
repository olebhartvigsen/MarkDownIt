import json, glob

for f in sorted(glob.glob('/workspace/MarkDownIt/tests/mermaid/golden/cls*.json')):
    g = json.load(open(f))
    print('==', f.split('/')[-1])
    for n in g['nodes']:
        texts = [n['title']] + n.get('annotations') or [] + n.get('members') or [] + n.get('methods') or []
        # annotations stored inner (no << >>)
        total = sum(len(t) for t in texts)
        total_w = sum(len(t) * 8 for t in texts)  # gBCR style? no
        print(f"  {n['id']:18s} w={n['w']:6.0f} h={n['h']:6.0f} nchars={total:3d} 4len+pad={total*4+24:5.0f} text='{n['title']}' ann={n.get('annotations')} mem={len(n.get('members') or [])} met={len(n.get('methods') or [])}")
        print('     texts:', texts)