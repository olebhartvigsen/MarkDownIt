base = '/workspace/MarkDownIt/tools/mermaid-oracle/node_modules/mermaid/dist/chunks/mermaid.esm.min/'
s = open(base + 'chunk-CF2LXAHU.mjs').read()
# Dt is insertEdge; find its registration to locate full extent
i = s.find('"insertEdge"')
# The function body: search backwards from registration for 'function Dt(' — but names differ; find via 'updatedPath' second occurrence region
# We know from earlier output that Dt starts near the string 'Dt=d(function(r,t,a,n,l,i,s){'
j = s.find('Dt=d(function(')
print('Dt at', j)
seg = s[j:j+5200]
print(seg)
