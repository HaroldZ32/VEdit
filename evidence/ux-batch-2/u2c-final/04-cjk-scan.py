#!/usr/bin/env python3
"""Acceptance scan: CJK in the first argument of tr()/translate(), and every CJK
string literal under src/ (comments stripped). Anything still listed must be an
allowed category; the script itself only reports."""
import os, re, sys

CJK = re.compile(r'[\u4e00-\u9fff]')
# translate("Ctx", "first arg")  and  tr("first arg")
TR = re.compile(r'(?:QCoreApplication::translate|translate|tr)\(\s*(?:"[^"]*"\s*,\s*)?"((?:[^"\\\n]|\\.)*)"')
LIT = re.compile(r'"((?:[^"\\\n]|\\.)*)"')

bad_tr, cjk_lits = [], []
for root, _dirs, files in os.walk('src'):
    for name in files:
        if not name.endswith(('.cpp', '.h')):
            continue
        path = os.path.join(root, name)
        for lineno, line in enumerate(open(path, encoding='utf-8', errors='replace').read().split('\n'), 1):
            code = line.split('//')[0]
            for m in TR.finditer(code):
                if CJK.search(m.group(1)):
                    bad_tr.append((path, lineno, m.group(1)))
            for m in LIT.finditer(code):
                if CJK.search(m.group(1)):
                    cjk_lits.append((path, lineno, m.group(1)))

print("tr()/translate() first arguments containing CJK:", len(bad_tr))
for row in bad_tr:
    print("  TR-CJK %s:%d %r" % row)
print("CJK string literals left under src/:", len(cjk_lits))
for path, lineno, text in cjk_lits:
    print("  LIT    %s:%d %r" % (path, lineno, text))
sys.exit(1 if bad_tr else 0)
