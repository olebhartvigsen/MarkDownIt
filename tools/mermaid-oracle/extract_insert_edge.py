import re
base = '/workspace/MarkDownIt/tools/mermaid-oracle/node_modules/mermaid/dist/chunks/mermaid.esm.min/'
s = open(base + 'chunk-CF2LXAHU.mjs').read()
# find the insertEdge implementation (class renderer inserts paths with updatedPath/originalPath)
m = re.search(r'[ud]?\(?function \w+\(([^)]*)\)\{[^{]*?updatedPath', s)
for m in re.finditer(r'function ([A-Za-z0-9_$]+)\(([^)]*)\)\{', s):
    idx = m.start()
    # crude: take 3500 chars, check for 'updatedPath'
    body = s[idx:idx+4500]
    if 'updatedPath' in body and ('calcEdgeIntersections' in body or 'intersection' in body):
        print('=== candidate at', idx, '===')
        print(body[:4300])
        print()
        break
