#!/usr/bin/env python3
"""T21 — apply the remaining tr() wrapping for the 215 UI candidates.

Corpus (never invented here):
  * evidence/i18n/07-triage-466.json   the frozen candidate triage
  * evidence/i18n/09-ui-translations.json  the 215 decisions + Chinese wording
  * evidence/i18n/03-glossary.md       the terminology the wording follows

The transformation is mechanical and reviewable: an unwrapped literal that is
one of the 215 UI candidates becomes
    QCoreApplication::translate("VEdit", "<source>")
(and the wrapper it sat in — QStringLiteral/QLatin1String/fromUtf8 — is
replaced, so no QStringLiteral(non-literal) can be produced). A file that needs
the call gets `#include <QCoreApplication>`.

Context choice: one explicit context, "VEdit", for the service/消息 strings that
have no natural QObject class; already wrapped `tr()` strings keep their class
context (the .ts is produced by lupdate from the real call sites).

The script never touches setObjectName()/setProperty() lines (object names and
JSON keys are identifiers, not UI text). `--apply` writes; without it the run is
a dry-run report.
"""
from __future__ import annotations

import argparse
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
UI_CORPUS = os.path.join(ROOT, 'evidence/i18n/09-ui-translations.json')
SRC = os.path.join(ROOT, 'src')
CONTEXT = 'VEdit'


def escape(s: str) -> str:
    return s.replace('\\', '\\\\').replace('"', '\\"')


def literal_re(s: str) -> re.Pattern:
    return re.compile('(?<![\\w])"' + re.escape(escape(s)) + '"')


WRAPPER_RE = re.compile(r'(QStringLiteral|QLatin1String|QString::fromUtf8|'
                        r'QString::fromLatin1)\s*\(\s*$')
TR_RE = re.compile(r'(?:\btr|QObject::tr|QCoreApplication::translate|translate)\s*\(\s*$')
DANGEROUS_RE = re.compile(r'(?:setObjectName|setProperty|setRegisteredObjectName|'
                          r'#include|constexpr|static const char|const char \*)')


def source_files() -> list[str]:
    out = []
    for root, _dirs, files in os.walk(SRC):
        for name in files:
            if name.endswith(('.cpp', '.h', '.cc', '.hpp')):
                out.append(os.path.join(root, name))
    return sorted(out)


def load_candidates() -> dict[str, dict]:
    rows = json.load(open(UI_CORPUS, encoding='utf-8'))
    return {r['id']: r for r in rows}


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('--apply', action='store_true')
    ap.add_argument('--only', default='', help='comma separated triage ids')
    args = ap.parse_args()

    candidates = load_candidates()
    only = {x.strip() for x in args.only.split(',') if x.strip()}
    wanted = [c for c in candidates.values() if c['decision'] == '译'
              and (not only or c['id'] in only)]
    by_string: dict[str, list[dict]] = {}
    for c in wanted:
        by_string.setdefault(c['string'], []).append(c)

    plan: dict[str, list[dict]] = {}
    skipped: list[tuple[str, str, str]] = []
    for path in source_files():
        text = open(path, encoding='utf-8').read()
        lines = text.split('\n')
        original_lines = list(lines)
        changes = []
        for s, cs in by_string.items():
            pattern = literal_re(s)
            for idx, line in enumerate(lines):
                m = pattern.search(line)
                if not m:
                    continue
                pre = line[:m.start()]
                if TR_RE.search(pre):
                    skipped.append((path, cs[0]['id'], 'already-wrapped'))
                    continue
                if DANGEROUS_RE.search(pre):
                    skipped.append((path, cs[0]['id'], 'dangerous-context: ' + pre.strip()[-40:]))
                    continue
                wm = WRAPPER_RE.search(pre)
                if wm and line.find(')', m.end()) < 0:
                    skipped.append((path, cs[0]['id'], 'multi-line wrapper'))
                    continue
                changes.append({'id': cs[0]['id'], 'string': s, 'line': idx + 1})
        if changes:
            plan[path] = changes

    for path, changes in sorted(plan.items()):
        print(f'{path}: {len(changes)} change(s)')
        for c in changes:
            print(f"   {c['id']} L{c['line']}: {c['string']!r}")
    print(f'\nTOTAL files={len(plan)} changes={sum(len(v) for v in plan.values())} '
          f'skipped={len(skipped)}')
    for s in skipped:
        print('SKIP', *s)

    if not args.apply:
        return 0

    for path, changes in plan.items():
        text = open(path, encoding='utf-8').read()
        lines = text.split('\n')
        for s in {c['string'] for c in changes}:
            pattern = literal_re(s)
            for idx, line in enumerate(lines):
                m = pattern.search(line)
                if not m:
                    continue
                pre = line[:m.start()]
                if TR_RE.search(pre) or DANGEROUS_RE.search(pre):
                    continue
                wm = WRAPPER_RE.search(pre)
                if wm:
                    close = line.find(')', m.end())
                    if close < 0:
                        continue
                    lines[idx] = line[:wm.start()] + \
                        'QCoreApplication::translate("%s", %s)' % (CONTEXT, line[m.start():m.end()]) + \
                        line[close + 1:]
                else:
                    lines[idx] = line[:m.start()] + \
                        'QCoreApplication::translate("%s", %s)' % (CONTEXT, line[m.start():m.end()]) + \
                        line[m.end():]
        new_text = '\n'.join(lines)
        if 'QCoreApplication::translate' in new_text and '#include <QCoreApplication>' not in new_text:
            inc_lines = [i for i, l in enumerate(lines) if l.startswith('#include')]
            at = (inc_lines[-1] + 1) if inc_lines else 2
            lines.insert(at, '#include <QCoreApplication>')
            new_text = '\n'.join(lines)
        open(path, 'w', encoding='utf-8').write(new_text)
    print('APPLIED')
    return 0


if __name__ == '__main__':
    sys.exit(main())
