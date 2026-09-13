import re
s = open('/workspace/MarkDownIt/tools/mermaid-oracle/node_modules/mermaid/dist/chunks/mermaid.esm.min/sequenceDiagram-NDJCBIXI.mjs').read()
i = s.find('"drawMessage"')
print('drawMessage reg at', i)
# registration: ]=u(function(e,t,c,o){...},"drawMessage") or similar; go back to find 'function'
start = s.rfind('function', 0, i)
print('fn start', start)
print(s[start:start+2600])