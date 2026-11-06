#!/usr/bin/env python3
"""T21 — merge the 215-candidate Chinese wording into the lupdate .ts.

Input (the only corpus):
  * evidence/i18n/09-ui-translations.json — the per-candidate decision + wording
  * translations/vedit_zh_CN.ts            — contexts/locations from lupdate

For every <message> whose <source> is a candidate decided "译", the Chinese
translation is written. Existing translations (the T18 seed) are never
overwritten. Candidates decided "不译" (theme identifiers) keep an empty
translation on purpose. A source string that appears in several contexts gets
the same wording in all of them (it is the same UI text).

Prints a coverage report: how many candidate strings are present in the .ts and
how many are translated, which is the AC-4.6 "no residual untranslated" figure.
"""
from __future__ import annotations

import json
import os
import sys
import xml.etree.ElementTree as ET

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
CORPUS = os.path.join(ROOT, 'evidence/i18n/09-ui-translations.json')
SUPPLEMENTARY = os.path.join(ROOT, 'evidence/i18n/10-ui-translations-supplementary.json')
TS = os.path.join(ROOT, 'translations/vedit_zh_CN.ts')


def main() -> int:
    rows = json.load(open(CORPUS, encoding='utf-8'))
    rows += json.load(open(SUPPLEMENTARY, encoding='utf-8'))
    corpus = {r['string']: r for r in rows}
    tree = ET.parse(TS)
    root = tree.getroot()
    applied = 0
    kept = 0
    present = set()
    translated_sources = set()
    for context in root.findall('context'):
        for message in context.findall('message'):
            source_el = message.find('source')
            if source_el is None or source_el.text is None:
                continue
            source = source_el.text
            row = corpus.get(source)
            if row is None:
                continue
            present.add(source)
            translation = message.find('translation')
            if translation is None:
                translation = ET.SubElement(message, 'translation')
            if row['decision'] == '译':
                translation.attrib.pop('type', None)
                translation.text = row['translation']
                applied += 1
                translated_sources.add(source)
            else:
                if translation.text:
                    kept += 1
    tree.write(TS, encoding='utf-8', xml_declaration=True)
    missing = sorted(set(corpus) - present)
    untranslated = sorted(s for s, r in corpus.items()
                          if r['decision'] == '译' and s not in translated_sources)
    print(f'candidates in corpus : {len(corpus)}')
    print(f'present in .ts      : {len(present)}')
    print(f'translations written: {applied}')
    print(f'kept existing ("不译" rows with text): {kept}')
    print(f'not present in .ts  : {len(missing)}')
    for s in missing:
        print('  MISSING', corpus[s]['id'], repr(s))
    print(f'untranslated        : {len(untranslated)}')
    for s in untranslated:
        print('  UNTRANSLATED', corpus[s]['id'], repr(s))
    return 0 if not missing and not untranslated else 1


if __name__ == '__main__':
    sys.exit(main())
