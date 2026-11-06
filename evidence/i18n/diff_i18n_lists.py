#!/usr/bin/env python3
"""Diff two i18n candidate lists and explain every difference (ADDENDUM A-16).

Inputs are the `path:line: <string>` logs produced by
`extract_i18n_candidates.py` (the format of `01-string-inventory.log`).

The diff aligns rows by (path, string), then by string alone, so a moved or
re-numbered candidate is not reported as a removal. It answers the three
questions the task asks about the old list:

  * ADDED    — candidates the new extraction finds that 01 did not have;
  * GONE     — 01 candidates the new extraction no longer emits, split into
               `removed` (the string is gone from the tree as well) and
               `rule-dropped` (the string still exists in the tree but the new
               rules exclude it — the rule is named);
  * DUPLICATES — strings that occur more than once in the new extraction while
               01 held one row per string.

Usage:
  python3 evidence/i18n/diff_i18n_lists.py \
      --old evidence/i18n/01-string-inventory.log \
      --old-head af85225 \
      --new evidence/i18n/05-extracted-current.log \
      --out evidence/i18n/06-extractor-diff.md \
      --json evidence/i18n/06-extractor-diff.json

Exit code 0 on success, 2 on a parse error.
"""
from __future__ import annotations

import argparse
import collections
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import extract_i18n_candidates as X  # noqa: E402

ROW_RE = re.compile(r'^(src/[^:]+):(\d+): (.*)$')


def parse_log(path: str):
    rows = []
    with open(path, encoding='utf-8') as fh:
        for line in fh:
            line = line.rstrip('\n')
            m = ROW_RE.match(line)
            if m:
                rows.append((m.group(1), int(m.group(2)), m.group(3)))
    return rows


def raw_literals(ref: str | None, include_headers=True):
    """Every string literal in the tree, no candidate rules: content -> set(paths)."""
    out = collections.defaultdict(set)
    for path in X.workspace_files(ref, include_headers):
        text = X.read_source(path, ref)
        if text is None:
            continue
        for (_line, lit, _ctx, _pfx) in X.scan_cpp(text):
            out[lit].add(path)
    return out


def candidate_index(rows):
    by_pair = collections.defaultdict(list)
    by_string = collections.defaultdict(list)
    for r in rows:
        by_pair[(r['path'], r['string'])].append(r)
        by_string[r['string']].append(r)
    return by_pair, by_string


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument('--old', default='evidence/i18n/01-string-inventory.log')
    ap.add_argument('--old-head', default='af85225',
                    help='git ref inside the old log\'s header (for reporting only)')
    ap.add_argument('--new', default='evidence/i18n/05-extracted-current.log')
    ap.add_argument('--out', default='evidence/i18n/06-extractor-diff.md')
    ap.add_argument('--json', default='evidence/i18n/06-extractor-diff.json')
    ap.add_argument('--quiet', action='store_true')
    args = ap.parse_args(argv)

    old = parse_log(args.old)
    new = parse_log(args.new)
    old_pairs = {(f, s) for f, _l, s in old}
    new_pairs = {(f, s) for f, _l, s in new}
    old_strings = {s for _f, _l, s in old}
    new_strings = {s for _f, _l, s in new}
    new_by_string = collections.defaultdict(list)
    for f, l, s in new:
        new_by_string[s].append((f, l))

    kept = old_pairs & new_pairs
    old_only = [(f, l, s) for f, l, s in old if (f, s) not in new_pairs]
    # verify every old_only row is a distinct string (01 has no duplicates)
    moved = []
    gone = []
    for f, l, s in old_only:
        if s in new_strings:
            moved.append({'old_source': f'{f}:{l}', 'string': s,
                          'new_sources': [f'{a}:{b}' for a, b in new_by_string[s]]})
        else:
            gone.append({'old_source': f'{f}:{l}', 'string': s})

    # enrich "gone": is the string still present anywhere in the current tree,
    # and if so, which rule kills it now?
    tree_lits = raw_literals(None, include_headers=True)
    occurrences = collections.defaultdict(list)   # string -> [(site, drop_reason, path)]
    for path in X.workspace_files(None, include_headers=True):
        text = X.read_source(path, None)
        if text is None:
            continue
        for (_line, lit, ctx, _pfx) in X.scan_cpp(text):
            site = X.classify_call_site(ctx)
            ok, why = X.candidate_kind(lit, ctx, path, keep_newlines=False)
            if ok and not re.search(r'\s', lit):
                ok, why = False, 'C10-no-whitespace'
            elif ok and site != 'qstringliteral':
                ok, why = False, 'C9-not-qstringliteral'
            occurrences[lit].append((site, why, path))
    for g in gone:
        present = tree_lits.get(g['string'])
        g['in_tree'] = bool(present)
        g['tree_paths'] = sorted(present)[:5] if present else []
        occ = occurrences.get(g['string'], [])
        reasons = ''
        if not occ:
            reasons = '字符串已从当前树删除'
        elif any(site == 'tr' for site, _w, _p in occ):
            reasons = '已被 tr() 包装（已翻译），不再属于 QStringLiteral 候选族'
        elif any(why == '' and site == 'qstringliteral' for site, why, _p in occ):
            reasons = '仍满足形状规则，但所在文件不在 .cpp 扫描范围'
        elif any(why == '' for _s, why, _p in occ):
            sites = sorted({site for site, why, _p in occ if why == ''})
            reasons = '调用点不是 QStringLiteral：' + ','.join(sites)
        else:
            dropped = collections.Counter(why for _s, why, _p in occ if why)
            reasons = '形状规则淘汰：' + ', '.join(f'{k}×{v}' for k, v in dropped.most_common())
        g['reason'] = reasons

    added = [(f, l, s) for f, l, s in new if (f, s) not in old_pairs and s not in old_strings]
    dup_occurrences = collections.Counter(s for _f, _l, s in new)
    duplicates = [{'string': s, 'count': c,
                   'sources': [f'{a}:{b}' for a, b in new_by_string[s]]}
                  for s, c in sorted(dup_occurrences.items(), key=lambda kv: -kv[1]) if c > 1]

    # additions grouped by area (first two path components) and whether the
    # area existed in the old list at all.
    old_areas = collections.Counter('/'.join(f.split('/')[:3]) for f, _l, _s in old)
    added_area = collections.Counter('/'.join(f.split('/')[:3]) for f, _l, _s in added)
    areas = []
    for area, n in added_area.most_common():
        areas.append({'area': area, 'added': n,
                      'in_old_list': old_areas.get(area, 0) > 0,
                      'old_count': old_areas.get(area, 0)})

    stats = {
        'old_rows': len(old), 'old_unique_strings': len(old_strings),
        'new_rows': len(new), 'new_unique_strings': len(new_strings),
        'kept_pairs': len(kept),
        'moved_rows': len(moved),
        'gone_rows': len(gone),
        'gone_removed_from_tree': sum(1 for g in gone if not g['in_tree']),
        'gone_rule_dropped': sum(1 for g in gone if g['in_tree']),
        'added_rows': len(added),
        'duplicate_strings': len(duplicates),
        'duplicate_occurrences': sum(d['count'] - 1 for d in duplicates),
    }

    doc = {'stats': stats, 'added_areas': areas, 'moved': moved, 'gone': gone,
           'duplicates': duplicates,
           'added': [{'source': f'{f}:{l}', 'string': s} for f, l, s in added]}
    with open(args.json, 'w', encoding='utf-8') as fh:
        json.dump(doc, fh, ensure_ascii=False, indent=1)
        fh.write('\n')

    md = render_markdown(args, stats, areas, moved, gone, duplicates, added)
    with open(args.out, 'w', encoding='utf-8') as fh:
        fh.write(md)
    if not args.quiet:
        print(json.dumps(stats, indent=1), file=sys.stderr)
    return 0


def render_markdown(args, stats, areas, moved, gone, duplicates, added):
    L = []
    L.append('# i18n 候选清单差异报告（01 → 可重现抽取器）')
    L.append('')
    L.append('引用：**ADDENDUM A-16**（T45 / i18n 前哨）。')
    L.append('')
    L.append(f'- 旧清单：`{args.old}`（git_head `{args.old_head}`，启发式抽取，无脚本）')
    L.append(f'- 新清单：`{args.new}`（`extract_i18n_candidates.py --profile strict`）')
    L.append('- 对齐方式：先按 `(path, string)` 精确对齐，再按 `string` 单独对齐（行号漂移不算消失）。')
    L.append('')
    L.append('## 1. 计数')
    L.append('')
    L.append('| 指标 | 旧清单 | 新清单 |')
    L.append('|---|---|---|')
    L.append(f"| 行数（occurrences） | {stats['old_rows']} | {stats['new_rows']} |")
    L.append(f"| 去重字符串 | {stats['old_unique_strings']} | {stats['new_unique_strings']} |")
    L.append('')
    L.append(f"- 两边 `(path,string)` 完全一致：**{stats['kept_pairs']}**")
    L.append(f"- 旧行位置变了但字符串还在新清单里（移动/重号）：**{stats['moved_rows']}**")
    L.append(f"- 旧行在新清单里消失：**{stats['gone_rows']}**"
             f"（其中字符串仍在树里、只是不再满足规则：{stats['gone_rule_dropped']}；"
             f"字符串已从树里消失：{stats['gone_removed_from_tree']}）")
    L.append(f"- 新清单新增：**{stats['added_rows']}**")
    L.append(f"- 新清单重复字符串：**{stats['duplicate_strings']}** 个串，"
             f"多出 **{stats['duplicate_occurrences']}** 次出现"
             '（旧清单是「一字符串一行」，新清单是「每个字面量一行」）')
    L.append('')
    L.append('## 2. 新增（added）按区域')
    L.append('')
    L.append('`in_old_list=false` 的区域 = 旧启发式整片没扫到（例如 `src/terminal/`）。')
    L.append('')
    L.append('| 区域 | 新增条数 | 旧清单同区域条数 | 旧清单覆盖过？ |')
    L.append('|---|---|---|---|')
    for a in areas:
        L.append(f"| `{a['area']}` | {a['added']} | {a['old_count']} | "
                 f"{'是' if a['in_old_list'] else '**否**'} |")
    L.append('')
    L.append('## 3. 消失（gone）逐条')
    L.append('')
    L.append('`in_tree` = 该字符串在当前树里是否仍存在；`reason` = 不再成为候选的原因。')
    L.append('')
    L.append('| 旧出处 | 字符串 | in_tree | 现路径 | reason |')
    L.append('|---|---|---|---|---|')
    for g in gone:
        s = g['string'].replace('|', '\\|')
        L.append(f"| `{g['old_source']}` | `{s}` | {g['in_tree']} | "
                 f"{', '.join('`'+p+'`' for p in g['tree_paths']) or '—'} | {g['reason']} |")
    L.append('')
    L.append('## 4. 移动（moved）逐条')
    L.append('')
    L.append('| 旧出处 | 字符串 | 新出处 |')
    L.append('|---|---|---|')
    for m in moved:
        s = m['string'].replace('|', '\\|')
        L.append(f"| `{m['old_source']}` | `{s}` | "
                 f"{', '.join('`'+p+'`' for p in m['new_sources'])} |")
    L.append('')
    L.append('## 5. 重号（duplicates）')
    L.append('')
    L.append('同一字符串在新清单里出现多次（旧清单按字符串去重，所以这些在 01 里只有一行）。')
    L.append('')
    L.append('| 次数 | 字符串 | 出处 |')
    L.append('|---|---|---|')
    for d in duplicates:
        s = d['string'].replace('|', '\\|')
        srcs = ', '.join('`'+x+'`' for x in d['sources'][:6])
        more = '' if len(d['sources']) <= 6 else f' …(+{len(d["sources"])-6})'
        L.append(f"| {d['count']} | `{s}` | {srcs}{more} |")
    L.append('')
    L.append('## 6. 新增（added）全量')
    L.append('')
    L.append('| 出处 | 字符串 |')
    L.append('|---|---|')
    for f, l, s in added:
        L.append(f"| `{f}:{l}` | `{s.replace('|', chr(92)+'|')}` |")
    L.append('')
    return '\n'.join(L)


if __name__ == '__main__':
    sys.exit(main())
