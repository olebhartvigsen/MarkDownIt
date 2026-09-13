import re
s = open('/workspace/MarkDownIt/tools/mermaid-oracle/node_modules/mermaid/dist/chunks/mermaid.core/chunk-DS2CGKN3.mjs').read()
i = s.find('textHelper')
print('first at', i)
i = s.find('function textHelper')
print('fn at', i)
if i < 0:
    # search for 'async function textHelper' or 'textHelper ='
    for m in re.finditer(r'textHelper', s):
        j = m.start()
        print(j, repr(s[j-30:j+40]))