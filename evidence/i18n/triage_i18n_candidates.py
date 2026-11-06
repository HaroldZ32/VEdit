#!/usr/bin/env python3
"""Triage the 01 i18n candidates: UI / non-UI / already-translated / obsolete.

ADDENDUM A-16 / T45. Inputs (no fabricated corpus):
  * `evidence/i18n/01-string-inventory.log` (466 candidates, stale line numbers)
  * the current working tree (the extractor's scanner supplies live context)
  * `evidence/i18n/03-glossary.md` (the single terminology authority)

Outputs:
  * `evidence/i18n/07-triage-466.csv`  — one machine-readable row per candidate
  * `evidence/i18n/07-triage-466.json` — same rows + header/stats
  * stdout — counts per class and per rule

Each row carries:
  id                T001..T466 (01 order)
  source_01         the stale `path:line` from 01
  string            the literal, verbatim (leading spaces preserved)
  classification    UI | non-UI | already-translated | obsolete
  reason            why (rule id + human sentence)
  glossary_note     `ok`, `keep-en:<term>`, `译:<term>=<zh>`, `conflict:<...>`,
                    `variant:<a>≠<b>`, `unlisted-term:<term>`
  confidence        high | medium | low
  current_sources   live `path:line` occurrences (up to 4)
  call_sites        live call-site classes (from the extractor's classifier)
  notes             anything a reviewer needs (dev surface, moved file, …)

The classifier is deliberately explainable: every row names the rule that
fired (`R01`..`R30`), so a reviewer can re-run with `--list-rule Rxx` and check
the definition instead of re-deriving the decision.

Rules (first match wins)
------------------------
R01 tr-wrapped          a live occurrence sits directly inside tr()/QObject::tr()
R02 chinese             the literal already is Chinese text (CJK present)
R03 removed             no live literal anywhere in src/ (comment-only or gone)
R04 dev-surface         file is a test/selftest/probe/bench/tool
R05 log-call            a live occurrence is a qWarning/qInfo/printf call
R06 object-key          identifier with no whitespace: Qt object name / JSON key
R07 shortcut            key binding (`Ctrl+S`, `F6`, …)
R08 css                 style-sheet fragment
R09 diag-template       machine-readable `key=%1 …` diagnostic line
R10 http-payload        `Authorization: Bearer …` style protocol payload
R11 code-snippet        Verilog/SystemVerilog source or instance text
R12 json-fragment       `{ this is not json`, `[1,2]`, …
R13 cli-help            command-line help/usage text (not a Qt surface)
R14 format-only         no word characters at all
R15 ui-call-site        a live occurrence is setText/setToolTip/menu/QMessageBox…
R16 human-label         sentence/label shape in a shipped (non-dev) source file
R17 uncertain           no decisive signal
"""
from __future__ import annotations

import argparse
import collections
import csv
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import extract_i18n_candidates as X  # noqa: E402

ROW_RE = re.compile(r'^(src/[^:]+):(\d+): (.*)$')
CJK_RE = re.compile(r'[\u4e00-\u9fff]')
DEV_PATH_RE = re.compile(r'(?:/tests?/|_selftest\.cpp$|/probes?/|/tools?/|_bench\.cpp$|/benchmarks?/)')
UI_CALL_SITES = {'ui_text', 'ui_action'}
LOG_CALL_SITES = {'log'}
KEY_RE = re.compile(r'^[A-Za-z_][A-Za-z0-9_]*(?:\.[A-Za-z0-9_]+)*$')
SHORTCUT_RE = re.compile(r'^(?:(?:Ctrl|Alt|Shift|Meta|Cmd)\+)*'
                         r'(?:[A-Z0-9]|F[0-9]{1,2}|Space|Esc|Tab|Home|End|PageUp|PageDown|'
                         r'Insert|Delete|Backspace|Left|Right|Up|Down)$')
CSS_RE = re.compile(r'(?:^|;)\s*(?:color|background(?:-color)?|padding|margin|border(?:-radius)?|'
                    r'font(?:-size|-family|-weight)?|display|min-width|max-width)\s*:', re.I)
DIAG_TEMPLATE_RE = re.compile(r'%[0-9]|%[sd]')
HTTP_RE = re.compile(r'(?i)^(authorization|cookie|content-type|user-agent|accept)\s*:')
VERILOG_KW_RE = re.compile(r'^\s*(?:assign|module|endmodule|always|always_ff|always_comb|initial|'
                           r'wire|reg|logic|localparam|parameter|input|output|inout|begin|end|'
                           r'if|else|case|endcase|for|generate|endgenerate|typedef|package|'
                           r'import|function|task|interface|program|class|defparam|specify)\b')
INSTANCE_RE = re.compile(r'^[a-z_][a-z0-9_]*\s+[a-z_][a-z0-9_]*$')
JSON_FRAGMENT_RE = re.compile(r'^\s*[\[{]')
CLI_OPTION_RE = re.compile(r'(?:^|\s)--[a-z][a-z0-9-]*')
HUMAN_LABEL_RE = re.compile(r'^(?:[A-Z][a-z]|[A-Z]{2,}\b|&|\.\.\.|…)\S*')
SENTENCE_RE = re.compile(r'^[A-Za-z0-9&(\[{][^\n]*?[.:!?…%]\s*$')


def parse_inventory(path: str):
    rows = []
    with open(path, encoding='utf-8') as fh:
        for line in fh:
            m = ROW_RE.match(line.rstrip('\n'))
            if m:
                rows.append((m.group(1), int(m.group(2)), m.group(3)))
    return rows


def scan_tree(ref: str | None, include_headers: bool = True):
    """string -> list of occurrence dicts across the tree."""
    occ = collections.defaultdict(list)
    for path in X.workspace_files(ref, include_headers):
        text = X.read_source(path, ref)
        if text is None:
            continue
        for (line, lit, ctx, _pfx) in X.scan_cpp(text):
            occ[lit].append({'path': path, 'line': line,
                             'site': X.classify_call_site(ctx),
                             'ctx': ctx})
    return occ


def parse_glossary(path: str):
    """Return (action_terms, keep_en_terms) parsed from 03-glossary.md."""
    action, keep = {}, set()
    with open(path, encoding='utf-8') as fh:
        text = fh.read()
    for line in text.split('\n'):
        line = line.strip()
        if not line.startswith('|') or set(line) <= set('|-: '):
            continue
        cells = [c.strip() for c in line.strip('|').split('|')]
        if len(cells) != 2:
            continue
        en, zh = cells
        if en in ('英文', '源串') or zh in ('中文', '译法'):
            continue
        for part in re.split(r'\s*/\s*', en):
            part = part.strip()
            if part:
                action.setdefault(part, zh)
    # the "技术名词（保留英文原文）" paragraph: backticked terms, comma/、 separated
    m = re.search(r'## 技术名词[^\n]*\n+(.*?)\n\n', text, re.S)
    if m:
        for term in re.findall(r'`([^`]+)`', m.group(1)):
            keep.add(term)
    return action, keep


# Variant spellings that the glossary does NOT list as canonical; used to flag
# terminology drift between candidates (the glossary stays the authority).
VARIANT_GROUPS = {
    'Settings': ['Preferences', 'Options', 'Configuration'],
    'Files': ['File browser', 'Explorer'],
    'Version Control (VCS)': ['Source Control', 'Version control'],
    'Outline': ['Structure', 'Symbols'],
    'Diagnostics': ['Problems'],
    'lint': ['Linter', 'Lint engine', 'LINT'],
    'LSP': ['language server', 'Language Server'],
    'filelist': ['file list', 'File list', 'Filelist'],
    'atomic save': ['atomic write', 'Atomic save'],
}
CANONICAL = {v: k for k, vs in VARIANT_GROUPS.items() for v in vs}
CANONICAL.update({k: k for k in VARIANT_GROUPS})


def glossary_note(s: str, action: dict, keep: set) -> str:
    notes = []
    for part, zh in action.items():
        if len(part) <= 3:
            continue
        if re.search(r'(?<![A-Za-z])' + re.escape(part) + r'(?![A-Za-z])', s, re.I):
            notes.append(f'译:{part}={zh}')
    for term in sorted(keep, key=len, reverse=True):
        if term in s:
            notes.append(f'keep-en:{term}')
    for variant, canonical in CANONICAL.items():
        if variant == canonical:
            continue
        if re.search(r'(?<![A-Za-z])' + re.escape(variant) + r'(?![A-Za-z])', s, re.I):
            notes.append(f'variant:{variant}≠{canonical}')
    return ';'.join(dict.fromkeys(notes)) if notes else 'ok'


def classify(string, live, old_occ, path_01, action, keep):
    """Return (classification, reason, confidence, notes)."""
    notes = []
    live_sites = {o['site'] for o in live}
    live_paths = [o['path'] for o in live]

    if any(o['site'] == 'tr' for o in live):
        return ('already-translated',
                'R01 已被 tr()/QObject::tr() 直接包装，无需再包',
                'high', notes)
    if CJK_RE.search(string):
        if live and all(DEV_PATH_RE.search(p) for p in live_paths):
            notes.append('中文文本，但只出现在开发自检文件里')
        return ('already-translated',
                'R02 字符串本身已是中文（CJK 文本），不需要翻译',
                'high', notes)
    if not live:
        return ('obsolete',
                'R03 当前树里已不存在该字面量（注释中残留或已删除/改写）'
                f'；01 出处 {path_01}',
                'high', notes)

    dev_only = all(DEV_PATH_RE.search(p) for p in live_paths)
    if dev_only:
        elsewhere = [o for o in live if not DEV_PATH_RE.search(o['path'])]
        if elsewhere:
            notes.append('同一字符串也出现在交付文件: ' +
                         ', '.join(f"{o['path']}:{o['line']}({o['site']})" for o in elsewhere[:3]))
        return ('non-UI',
                'R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），'
                '不是交付 UI 表面',
                'high', notes)
    if live_sites & LOG_CALL_SITES and not (live_sites & UI_CALL_SITES):
        return ('non-UI', 'R05 只出现在 qDebug/qInfo/qWarning/printf 日志调用点',
                'high', notes)

    s = string.strip()
    if not re.search(r'\s', s) and (KEY_RE.match(s) or re.match(r'^[a-z][a-z0-9_.-]+$', s)):
        return ('non-UI', 'R06 无空白的标识符形状：Qt objectName / JSON 键 / 命令 id',
                'high', notes)
    if SHORTCUT_RE.match(s) and ('+' in s or s.startswith('F')):
        return ('non-UI', 'R07 快捷键字符串（菜单里作为 shortcut 显示，不翻译）',
                'high', notes)
    if CSS_RE.search(s):
        return ('non-UI', 'R08 Qt 样式表片段（CSS 属性）', 'high', notes)
    if DIAG_TEMPLATE_RE.search(s) and re.search(r'[A-Za-z_][A-Za-z0-9_]*=', s):
        return ('non-UI', 'R09 机器可读诊断模板（key=%1 …），供日志/门禁解析',
                'high', notes)
    if HTTP_RE.search(s):
        return ('non-UI', 'R10 HTTP 协议载荷（Authorization 头示例）', 'high', notes)
    if VERILOG_KW_RE.match(s) or INSTANCE_RE.match(s) or re.match(r'^\s*\+', s) \
            or (';' in s and re.search(r'\b(?:assign|module|wire|reg|begin|end)\b', s)):
        return ('non-UI', 'R11 Verilog/SystemVerilog 代码片段或实例化文本（测试夹具/示例）',
                'high', notes)
    if JSON_FRAGMENT_RE.match(s) or re.match(r'^["\'][^"\']+["\']\s*[:,]', s):
        return ('non-UI', 'R12 JSON/代码片段（非法 JSON 测试输入）', 'high', notes)
    if CLI_OPTION_RE.search(s) or (path_01.endswith('src/app/main.cpp') and
                                   re.search(r'(?:usage|option|flag|argument|--[a-z])', s, re.I)):
        return ('non-UI', 'R13 命令行（CLI）帮助/用法文本，不走 Qt 翻译管线',
                'medium', notes)
    if not re.search(r'[A-Za-z\u4e00-\u9fff]', s):
        return ('non-UI', 'R14 无任何单词字符（纯格式/标点）', 'high', notes)

    if live_sites & UI_CALL_SITES:
        site = ','.join(sorted(live_sites & UI_CALL_SITES))
        return ('UI', f'R15 出现在 Qt UI 调用点（{site}）', 'high', notes)
    if re.search(r'\b(?:expected|expect|got|assert|fail|failure|checks?|strcmp)\b', s, re.I) \
            and not re.search(r'\b(?:Cannot|Could not|No |Name |Save|Open|Close)\b', s):
        return ('non-UI', 'R16 断言/校验模板（测试期望文本）', 'high', notes)

    # --- message/label shapes in shipped code --------------------------------
    code_punct = bool(re.search(r';|\{|\}|==|->(?!>)', s)) and not re.search(r'%\d', s)
    words = re.findall(r'[A-Za-z]{2,}', s)
    message_start = bool(re.match(r'(?:Cannot|Could not|Couldn|Failed|Failure|No |Name |'
                                  r'Please|Select|Open|Close|Save|Delete|Create|Rename|'
                                  r'Refresh|Reload|Start|Stop|Run|Show|Hide|Enter|Choose|'
                                  r'Project|Session|Theme|File|Folder|Search|Diagnostics|'
                                  r'Outline|Terminal|Editor|Settings|Version|Warning|Error)\b',
                                  s))
    lower_verb = bool(re.match(r'(?:cannot|failed|error|unable|invalid|missing|unknown|'
                               r'opening|saving|loading|reading|writing|parsing)\b', s))
    if not code_punct and len(words) >= 2 and (message_start or lower_verb
                                              or SENTENCE_RE.match(s)):
        return ('UI', 'R17 人类可读的消息/标签，位于交付源文件（间接显示或由上层弹出）',
                'medium', notes)
    if not code_punct and re.search(r'[（(…]', s) and len(s.strip()) >= 4:
        return ('UI', 'R18 括号/省略号修饰的界面标签（附加在状态文本上）',
                'medium', notes)
    if code_punct and len(words) >= 2 and (message_start or lower_verb):
        return ('uncertain', 'R19 形似错误消息但含代码标点（:: () -> ），需人工确认显示点',
                'low', notes)

    if re.search(r'(?i)fault[- ]injection|injected|selftest|self-check|probe|'
                 r'development fallback|registry entry|not built here', s):
        return ('non-UI', 'R20 开发/故障注入诊断文本（fault-injection、selftest、probe）',
                'high', notes)
    if re.match(r'^[<{\[]', s) or '->' in s:
        return ('non-UI', 'R21 代码符号/数据片段（以符号开头或含 ->）', 'high', notes)
    if live_sites and live_sites <= {'selftest_out', 'test'}:
        return ('non-UI', 'R22 只出现在自检断言/输出调用点（self-check expectation）',
                'medium', notes)
    if len(words) >= 2 and not code_punct:
        return ('UI', 'R23 英文消息/状态文本（服务层以 message/error 字段传到界面，'
                      '显示点未逐条证明）', 'medium', notes)
    if len(words) == 1 and re.search(r'[.:…)]$', s.strip()):
        return ('UI', 'R24 单词+标点的界面标签（主题名/状态词）', 'low', notes)
    return ('uncertain', 'R25 无决定信号：既不像代码/键，也没有 UI 调用点',
            'low', notes)


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument('--inventory', default='evidence/i18n/01-string-inventory.log')
    ap.add_argument('--glossary', default='evidence/i18n/03-glossary.md')
    ap.add_argument('--out', default='evidence/i18n/07-triage-466.csv')
    ap.add_argument('--json', default='evidence/i18n/07-triage-466.json')
    ap.add_argument('--list-class', default=None, help='print rows of one class')
    ap.add_argument('--list-rule', default=None, help='print rows of one rule id')
    args = ap.parse_args(argv)

    old = parse_inventory(args.inventory)
    live_all = scan_tree(None)
    action, keep = parse_glossary(args.glossary)

    rows = []
    for i, (path_01, line_01, string) in enumerate(old, 1):
        live = live_all.get(string, [])
        cls, reason, conf, notes = classify(string, live, None, path_01, action, keep)
        g = glossary_note(string, action, keep)
        sources = [f"{o['path']}:{o['line']}" for o in live]
        sites = sorted({o['site'] for o in live})
        uniq_notes = list(dict.fromkeys(notes))
        if len(sources) > 1:
            uniq_notes.append(f'当前树共 {len(sources)} 处出现')
        if live and path_01.rsplit(':', 1)[0] != live[0]['path']:
            uniq_notes.append('出处路径已变化: ' + path_01 + ' → ' + live[0]['path'])
        rows.append({
            'id': f'T{i:03d}',
            'source_01': f'{path_01}:{line_01}',
            'string': string,
            'classification': cls,
            'reason': reason,
            'glossary_note': g,
            'confidence': conf,
            'current_sources': ';'.join(sources[:4]),
            'call_sites': ','.join(sites) if sites else 'none',
            'notes': '; '.join(uniq_notes) if uniq_notes else '',
        })

    fields = ['id', 'source_01', 'string', 'classification', 'reason',
              'glossary_note', 'confidence', 'current_sources', 'call_sites', 'notes']
    with open(args.out, 'w', newline='', encoding='utf-8') as fh:
        w = csv.DictWriter(fh, fieldnames=fields, lineterminator='\n')
        w.writeheader()
        w.writerows(rows)

    counts = collections.Counter(r['classification'] for r in rows)
    conf = collections.Counter(r['confidence'] for r in rows)
    rule_counts = collections.Counter(r['reason'].split()[0] for r in rows)
    with open(args.json, 'w', encoding='utf-8') as fh:
        json.dump({'header': {
            'task': 'T45 / ADDENDUM A-16',
            'inventory': args.inventory,
            'glossary': args.glossary,
            'rows': len(rows),
            'counts': dict(counts),
            'confidence': dict(conf),
            'rules': dict(sorted(rule_counts.items())),
        }, 'rows': rows}, fh, ensure_ascii=False, indent=1)
        fh.write('\n')

    if args.list_class:
        for r in rows:
            if r['classification'] == args.list_class:
                print(f"{r['id']}\t{r['source_01']}\t{r['reason'].split()[0]}\t"
                      f"{r['string']!r}\t{r['call_sites']}")
        return 0
    if args.list_rule:
        for r in rows:
            if r['reason'].startswith(args.list_rule):
                print(f"{r['id']}\t{r['source_01']}\t{r['string']!r}\t{r['call_sites']}")
        return 0

    print('rows:', len(rows), 'counts:', dict(counts), 'confidence:', dict(conf))
    print('rules:', dict(sorted(rule_counts.items())))
    return 0


if __name__ == '__main__':
    sys.exit(main())
