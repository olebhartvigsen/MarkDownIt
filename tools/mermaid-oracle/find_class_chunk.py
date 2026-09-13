import re
s = open('/workspace/MarkDownIt/tools/mermaid-oracle/node_modules/mermaid/dist/chunks/mermaid.core/chunk-F7MYA6JM.mjs').read()
print('len', len(s))
# class box drawing: search for patterns like 'width' near 'rect' / 'divider' / 'getClassBox'
for key in ['getClassBox', 'class_merge', 'divider', 'boxMargin', 'foreignObject', 'setClass', 'classBox']:
    i = s.find(key)
    if i > 0:
        print('===', key, 'at', i)
        print(s[max(0,i-300):i+500].replace('\n', ' ')[:800])
        print()