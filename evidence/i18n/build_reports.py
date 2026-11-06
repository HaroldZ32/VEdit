#!/usr/bin/env python3
"""T21 — build the checkable AC-4.5 / AC-4.6 evidence from the frozen corpus.

Outputs (all under evidence/i18n/):
  11-reconciliation-466.csv   one row per 01 candidate, with decision + reason
  11-reconciliation-466.md    the same, grouped, with the count table
  12-ui-blocks.md             the 215 UI candidates as per-file blocks:
                              id, source string, Chinese wording, landing call
                              site (file:line + translation context), decision

Inputs are the frozen corpus only:
  evidence/i18n/07-triage-466.json      classifications + rules
  evidence/i18n/09-ui-translations.json 215 UI decisions + wording
  evidence/i18n/10-ui-translations-supplementary.json
                                         already-tr() English UI, worded here
  translations/vedit_zh_CN.ts           the built translation table (lupdate)

Nothing is invented: a row's decision comes from its triage class; a UI row's
wording comes from the glossary-based corpus file (the two theme identifiers are
the only "不译" UI rows, with the reason recorded).
"""
from __future__ import annotations

import collections
import csv
import json
import os
import re
import xml.etree.ElementTree as ET

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
I18N = os.path.join(ROOT, 'evidence/i18n')
TRIAGE = os.path.join(I18N, '07-triage-466.json')
UI_CORPUS = os.path.join(I18N, '09-ui-translations.json')
SUPP = os.path.join(I18N, '10-ui-translations-supplementary.json')
TS = os.path.join(ROOT, 'translations/vedit_zh_CN.ts')
OUT_CSV = os.path.join(I18N, '11-reconciliation-466.csv')
OUT_MD = os.path.join(I18N, '11-reconciliation-466.md')
OUT_BLOCKS = os.path.join(I18N, '12-ui-blocks.md')
OUT_COV = os.path.join(I18N, '13-coverage.md')

W1 = re.compile(r'(?:\btr|QObject::tr)\s*\(\s*$')
W2 = re.compile(r'(?:QCoreApplication::translate|translate)\s*\(\s*"([^"]*)"\s*,\s*$')
DEFAULT_UNCERTAIN = '保留英文（AC-4.5 默认口径：未判定 → keep-en + 登记理由）'


def esc(s: str) -> str:
    return s.replace('\\', '\\\\').replace('"', '\\"')


def source_texts() -> dict[str, str]:
    out = {}
    for root, _dirs, files in os.walk(os.path.join(ROOT, 'src')):
        for name in files:
            if name.endswith(('.cpp', '.h')):
                path = os.path.join(root, name)
                out[os.path.relpath(path, ROOT)] = open(path, encoding='utf-8').read()
    return out


def landings(string: str, texts: dict[str, str]) -> list[tuple[str, int, str]]:
    pattern = re.compile('(?<![\\w])"' + re.escape(esc(string)) + '"')
    found = []
    for path, text in texts.items():
        for m in pattern.finditer(text):
            pre = text[max(0, m.start() - 80):m.start()]
            line = text[:m.start()].count('\n') + 1
            ctx = ''
            if W1.search(pre):
                ctx = 'class tr()'
            else:
                w2 = W2.search(pre)
                if w2:
                    ctx = w2.group(1)
            if ctx:
                found.append((path, line, ctx))
    return found


def ts_state() -> tuple[dict, dict]:
    """(source -> translated?, context -> [(source, translated?)]) from the .ts."""
    tree = ET.parse(TS)
    per_context = collections.defaultdict(list)
    per_source = collections.defaultdict(list)
    for context in tree.getroot().findall('context'):
        name = context.findtext('name') or ''
        for message in context.findall('message'):
            source = message.findtext('source') or ''
            tr = message.find('translation')
            ok = tr is not None and (tr.text or '').strip() != ''
            per_context[name].append((source, ok))
            per_source[source].append((name, ok))
    return per_source, per_context


def main() -> int:
    triage = json.load(open(TRIAGE, encoding='utf-8'))
    rows = triage['rows']
    ui_corpus = {r['id']: r for r in json.load(open(UI_CORPUS, encoding='utf-8'))}
    supp = {r['id']: r for r in json.load(open(SUPP, encoding='utf-8'))}
    texts = source_texts()
    per_source, per_context = ts_state()

    decision_rows = []
    counts = collections.Counter()
    decision_counts = collections.Counter()

    for r in rows:
        cls = r['classification']
        decision = ''
        reason = ''
        translation = ''
        if cls == 'UI':
            corpus_row = ui_corpus.get(r['id'])
            if corpus_row is None:
                decision, reason = '待定', '语料缺行（内部错误）'
            elif corpus_row['decision'] == '译':
                decision = '译'
                translation = corpus_row['translation']
                reason = (f"{r['reason']}；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，"
                          f"落点见 12-ui-blocks.md")
            else:
                decision = '不译'
                reason = f"{r['reason']}；{corpus_row['reason']}"
        elif cls == 'already-translated':
            decision = '已译'
            if r['id'] in supp:
                translation = supp[r['id']]['translation']
                reason = f"{r['reason']}；英文源串已补中文（10-ui-translations-supplementary.json）"
            elif any(ord(ch) > 0x2E7F for ch in r['string']):
                reason = (f"{r['reason']}；源串本身是中文（另一 lane 已落），"
                          f"英文界面下仍显示中文 —— 登记为已知缺陷，不在 AC-4.5/4.6 写域内")
            else:
                reason = f"{r['reason']}；tr() 已包装，中文条目随本次 .ts 合并"
        elif cls == 'obsolete':
            decision = '删除'
            reason = f"{r['reason']}；当前树中已无该字面量，无需代码动作"
        elif cls == 'non-UI':
            decision = '不译'
            reason = f"{r['reason']}（非 UI 表面，不进入 .ts）"
        elif cls == 'uncertain':
            decision = '待定→保留英文'
            reason = f"{r['reason']}；{DEFAULT_UNCERTAIN}"
        else:
            decision = '待定'
            reason = f"未知分类 {cls}"

        counts[cls] += 1
        decision_counts[decision] += 1
        decision_rows.append({
            'id': r['id'],
            'source_01': r['source_01'],
            'string': r['string'],
            'classification': cls,
            'decision': decision,
            'translation': translation,
            'reason': reason,
            'glossary_note': r['glossary_note'],
            'confidence': r['confidence'],
        })

    # --- CSV -------------------------------------------------------------
    with open(OUT_CSV, 'w', newline='', encoding='utf-8') as fh:
        writer = csv.DictWriter(fh, fieldnames=list(decision_rows[0].keys()))
        writer.writeheader()
        writer.writerows(decision_rows)

    # --- MD: counts + grouped table --------------------------------------
    expected = {'UI': 215, 'non-UI': 195, 'already-translated': 31,
                'obsolete': 15, 'uncertain': 10}
    lines = []
    lines.append('# T21 — 466 候选逐条对账报告（AC-4.5）\n')
    lines.append('Cell `cell-20261102T100600176Z` · task **T21** (`i18n-t21`) · member **se-i18n**\n')
    lines.append('语料（唯一输入，未自造）：`07-triage-466.json`（466 行分类）、'
                 '`02-bilingual-table-draft.md`、`03-glossary.md`；'
                 '译文落点：`09-ui-translations.json`、`12-ui-blocks.md`。\n')
    lines.append('## 1. 计数核对\n')
    lines.append('| 分类 | 行数 | 期望 | 结果 |\n|---|---|---|---|')
    for cls, want in expected.items():
        lines.append(f'| {cls} | {counts[cls]} | {want} | '
                     f'{"OK" if counts[cls] == want else "MISMATCH"} |')
    lines.append(f'| **合计** | **{sum(counts.values())}** | **466** | '
                 f'{"OK" if sum(counts.values()) == 466 else "MISMATCH"} |\n')
    lines.append('## 2. 决策分布\n')
    lines.append('| decision | 条数 |\n|---|---|')
    for d, n in decision_counts.most_common():
        lines.append(f'| {d} | {n} |')
    lines.append('')
    lines.append('`uncertain` 的 10 条按合同默认口径处理：**保留英文 + 登记理由**'
                 '（decision 记 `待定→保留英文`）。\n')
    lines.append('## 3. 逐条表（466 行）\n')
    lines.append('| id | source_01 | classification | decision | string | reason |')
    lines.append('|---|---|---|---|---|---|')
    for row in decision_rows:
        text = row['string'].replace('|', '\\|')
        reason = row['reason'].replace('|', '\\|')
        lines.append(f"| {row['id']} | `{row['source_01']}` | {row['classification']} | "
                     f"**{row['decision']}** | `{text}` | {reason} |")
    open(OUT_MD, 'w', encoding='utf-8').write('\n'.join(lines) + '\n')

    # --- 12-ui-blocks.md --------------------------------------------------
    blocks = collections.OrderedDict()
    missing_ts = []
    missing_landing = []
    for row in rows:
        if row['classification'] != 'UI':
            continue
        corpus_row = ui_corpus.get(row['id'], {})
        decision = '译' if corpus_row.get('decision') == '译' else '不译'
        translation = corpus_row.get('translation', '')
        found = landings(row['string'], texts)
        contexts = sorted({ctx for _p, _l, ctx in found})
        ts_ok = any(ok for name, ok in per_source.get(row['string'], []))
        if decision == '译' and not ts_ok:
            missing_ts.append((row['id'], row['string']))
        if decision == '译' and not found:
            missing_landing.append((row['id'], row['string']))
        key = row['source_01'].rsplit(':', 1)[0]
        blocks.setdefault(key, []).append((row, decision, translation, found, contexts, ts_ok))

    blines = []
    blines.append('# T21 — 215 条 UI 候选的可核对块（AC-4.6）\n')
    blines.append('每个块 = 一个源文件。列：triage id、源字符串、中文译文、'
                  '代码中的**落点**（包装调用 file:line + 翻译上下文）、.ts 状态。\n')
    blines.append(f'- UI 候选总数：**{sum(len(v) for v in blocks.values())}**（期望 215）\n')
    blines.append(f'- 未进 .ts 的译项：**{len(missing_ts)}**；无包装落点的译项：'
                  f'**{len(missing_landing)}**\n')
    if missing_ts:
        for item in missing_ts:
            blines.append(f'  - MISSING .ts: {item[0]} `{item[1]}`')
    if missing_landing:
        for item in missing_landing:
            blines.append(f'  - MISSING landing: {item[0]} `{item[1]}`')
    for path, items in blocks.items():
        blines.append(f'\n## `{path}`（{len(items)} 条）\n')
        blines.append('| id | 源串 | 中文 | 落点 | 上下文 | .ts |')
        blines.append('|---|---|---|---|---|---|')
        for row, decision, translation, found, contexts, ts_ok in items:
            if decision == '不译':
                translation_cell = '—（不译，见 11 报告）'
            else:
                translation_cell = translation
            landing = '<br>'.join(f'{p}:{ln}' for p, ln, _c in found) or '—'
            ctx = ', '.join(contexts) or '—'
            blines.append(f"| {row['id']} | `{row['string'].replace('|', chr(92)+'|')}` | "
                          f"{translation_cell.replace('|', chr(92)+'|')} | {landing} | {ctx} | "
                          f"{'OK' if ts_ok or decision == '不译' else 'MISSING'} |")
    open(OUT_BLOCKS, 'w', encoding='utf-8').write('\n'.join(blines) + '\n')

    # --- 13-coverage.md ---------------------------------------------------
    total_ts = sum(len(v) for v in per_context.values())
    finished_ts = sum(1 for v in per_context.values() for _s, ok in v if ok)
    cv = []
    cv.append('# T21 — 覆盖率/计数汇总（AC-4.5 / 4.6 / 4.7 自查）\n')
    cv.append('## 466 对账\n')
    cv.append(f'- 行数：{sum(counts.values())}（期望 466）')
    for cls, want in expected.items():
        cv.append(f'- {cls}: {counts[cls]}（期望 {want}）')
    cv.append(f'- 决策分布：{dict(decision_counts)}')
    cv.append('\n## 215 UI 译文落盘\n')
    cv.append(f'- UI 候选：215；决定译：{sum(1 for k, v in ui_corpus.items() if v["decision"] == "译")}；'
              f'决定不译：{sum(1 for k, v in ui_corpus.items() if v["decision"] != "译")}')
    cv.append(f'- 译项全部在 .ts：{"OK" if not missing_ts else "MISSING " + str(missing_ts)}')
    cv.append(f'- 译项全部有包装落点：{"OK" if not missing_landing else "MISSING " + str(missing_landing)}')
    cv.append('\n## translations/vedit_zh_CN.ts\n')
    cv.append(f'- messages：{total_ts}；有译文：{finished_ts}；'
              f'未译：{total_ts - finished_ts}（未译条目是非 UI 候选或 AC-4.5 判为不译的串）')
    cv.append('\n## 上下文（lupdate 实际产出）\n')
    for name in sorted(per_context):
        n = len(per_context[name])
        done = sum(1 for _s, ok in per_context[name] if ok)
        cv.append(f'- `{name}`：{n} 条，已译 {done}')
    open(OUT_COV, 'w', encoding='utf-8').write('\n'.join(cv) + '\n')

    print('wrote', OUT_CSV)
    print('wrote', OUT_MD)
    print('wrote', OUT_BLOCKS)
    print('wrote', OUT_COV)
    print('classification counts', dict(counts))
    print('decision counts', dict(decision_counts))
    print('missing ts', len(missing_ts), 'missing landing', len(missing_landing))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
