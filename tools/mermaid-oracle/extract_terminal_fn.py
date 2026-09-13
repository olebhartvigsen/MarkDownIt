import re
s = open('/workspace/MarkDownIt/tools/mermaid-oracle/node_modules/mermaid/dist/chunks/mermaid.esm.min/chunk-IIWVAQKY.mjs').read()
print('file len', len(s))
for fn in ['calcTerminalLabelPosition', 'calcLabelPosition']:
    # minified u-registration: u(NAME,"fnName") possibly with d(...) wrapper
    m = re.search(r'[ud]\((\w+),.{0,3}"' + fn + '"', s)
    print(fn, 'match:', m)
    if not m:
        idx = s.find(fn)
        print('raw find:', idx)
        if idx > 0:
            print(s[max(0, idx - 150):idx + 150])
        continue
    n = m.group(1)
    fi = s.rfind('function ' + n, 0, m.start())
    if fi < 0:
        fi = s.rfind('const ' + n, 0, m.start())
    if fi < 0:
        fi = s.rfind(n + '=', 0, m.start())
    print(s[fi:fi + 2400])
    print()
