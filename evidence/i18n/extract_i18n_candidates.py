#!/usr/bin/env python3
"""Reproducible i18n candidate extractor for VEdit (ADDENDUM A-16 / T45).

WHY THIS EXISTS
---------------
`evidence/i18n/01-string-inventory.log` (466 candidates) was produced by a
heuristic that was never shipped as code, over a *dirty* working tree (its
recorded git_head is af85225, but several recorded line numbers do not exist
at af85225). It therefore cannot be regenerated. This script is the durable
replacement: it re-derives a candidate list from any git ref or from the
working tree, with the rules written down and versioned, and with call-site
context so the T21 triage/wrapping work can be reviewed.

WHAT IT DOES
------------
1. Lexes every C/C++ source under `src/` (comments, char literals, raw strings
   and preprocessor include paths are skipped by a small state machine, not a
   regex over lines).
2. Records each double-quoted string literal with file, line, raw content,
   the literal prefix (u8/L/...), and the call-site context: the 96 characters
   of code that precede the literal plus a classified call-site kind.
3. Applies the **candidate rules** (C1..C8, see RULES below) that approximate
   and document the 01 heuristic.
4. Emits the list in the same `path:line: <string>` shape as 01 (so the two
   can be diffed), or as CSV/JSON with the full metadata.

TREE STATE
----------
The tree is in flight during cell cell-20261102T050548232Z (no cell commit is
allowed). Every run stamps the header with: UTC timestamp, `git rev-parse
HEAD`, the number of dirty paths, and a sha256 manifest of the scanned files,
so a reader can tell exactly which bytes were classified. The script itself is
the stable artifact; a list is only a snapshot.

USAGE
-----
  # working tree, log format (default: evidence/i18n/05-extracted-current.log)
  python3 evidence/i18n/extract_i18n_candidates.py

  # a git ref, e.g. the head the 01 inventory recorded
  python3 evidence/i18n/extract_i18n_candidates.py --ref af85225 --out /dev/stdout

  # machine-readable rows for triage
  python3 evidence/i18n/extract_i18n_candidates.py --format csv   --out evidence/i18n/05-extracted-current.csv
  python3 evidence/i18n/extract_i18n_candidates.py --format json  --out evidence/i18n/05-extracted-current.json

  # every literal (no candidate filter) with features, for rule tuning
  python3 evidence/i18n/extract_i18n_candidates.py --all --format csv --out /dev/stdout

RULES (candidate = literal that survives all of them; v1)
---------------------------------------------------------
C1 drop empty / whitespace-only literals.
C2 drop preprocessor include/import paths (`#include "x.h"`).
C3 drop pure paths / file names: no whitespace, and either every '/'-separated
   segment is a plain name (`rtl/include`, `foo.cpp`) or it is a single token
   ending in a known source/data extension.
C4 drop pure format specifiers: only `%1..%9`/`%s`/`%d` and punctuation
   (`"%1: %2"`, `"%1/%2"`), i.e. no word characters at all.
C5 drop identifier-shaped all-lowercase tokens: no whitespace, no uppercase
   letter, and only [a-z0-9._/-] characters (`file.close-pane`,
   `editor-pane-active-badge`, `systemverilog`, `text/plain`, `utf-8`).
   This is the rule that most of the 01 false-positive class shares: 01 kept
   camelCase keys/object names (`dockState`, `fileTreeView`, `nextPane`),
   which C5 also keeps, because distinguishing them needs context, not shape.
C6 drop Qt/C++ style-sheet or data fragments that carry no word: no letters
   outside `%` specs (covers `%;`, `#fff`, `* { }`).
C7 drop single-character literals.
C8 (kept, but flagged by the triage table, not by the extractor) literals in
   `src/**/tests/`, `*_selftest.cpp`, `src/editor/probes/` are developer
   surfaces: they are candidates for classification but not for `tr()`.

The rules are shape-based on purpose: the same rules run on any tree, so the
diff between two runs is explainable by string content rather than by guesswork
about line numbers.

EXIT CODES
----------
0 success; 2 usage/scan error.
"""
from __future__ import annotations

import argparse
import collections
import csv
import datetime as _dt
import hashlib
import json
import os
import re
import subprocess
import sys

RULES_VERSION = 'v1'
SOURCE_EXTS = ('.cpp', '.h', '.c', '.cc', '.hpp')
DEFAULT_OUT = 'evidence/i18n/05-extracted-current.log'

# A literal is "identifier shaped, all lowercase" when it has no whitespace,
# no uppercase letter, and only identifier/path punctuation.
LOWERCASE_TOKEN_RE = re.compile(r'^[a-z0-9][a-z0-9._/-]*$')
# A path/file name: slash separated plain names, or a known extension.
PLAIN_SEGMENT_RE = re.compile(r'^[A-Za-z0-9_.@+-]+$')
DATA_EXT_RE = re.compile(
    r'\.(?:cpp|cc|cxx|c|h|hpp|hh|hxx|inl|ipp|sv|svh|v|vh|py|sh|md|txt|json|jsonc|'
    r'toml|yml|yaml|xml|css|qss|ui|qrc|ts|cmake|ninja|log|ini|cfg|conf)$')
FORMAT_ONLY_RE = re.compile(r'^[%\d\s.,:;!?()\[\]{}<>\|/_\\*+\-=&#@^~\'"`]*$')

# Call-site classification. Two families:
#   ANY_PATTERNS  — a call anywhere in the enclosing statement says where the
#                   value goes (`addCommand(id, QStringLiteral("&Close Pane"), …)`;
#                   `statusLabel_->setText(tr("…"))`).
#   TAIL_PATTERNS — only meaningful immediately before the literal
#                   (`setObjectName(QStringLiteral("fileTreeView"))`).
# The first match in priority order wins: a statement can mention several calls,
# and the outermost/intent-bearing one is what the triage needs.
ANY_PATTERNS = [
    ('ui_text',   re.compile(r'\b(?:setText|setWindowTitle|setToolTip|setStatusTip|setWhatsThis|'
                             r'setPlaceholderText|setAccessibleName|setAccessibleDescription|'
                             r'addItem|addItems|addTab|setTabText|setTabToolTip|setTitle|'
                             r'setLabelText|setButtonText|setHeaderLabels|setHorizontalHeaderLabels|'
                             r'new QLabel|new QPushButton|new QCheckBox|new QRadioButton|new QGroupBox|'
                             r'new QToolButton|new QMenu|QMessageBox::\w+|QInputDialog::\w+)\s*\(')),
    ('ui_action', re.compile(r'\b(?:addCommand|addAction|addMenu|addDockWidget|setIconText|'
                             r'QAction|new QAction|setShortcut|setShortcutContext)\s*\(')),
    ('test',      re.compile(r'\b(?:QCOMPARE|QVERIFY|QVERIFY2|QEXPECT_FAIL|QTest::|QFETCH|'
                             r'QSignalSpy|QTRY_COMPARE|QTRY_VERIFY)\s*\(')),
    ('log',       re.compile(r'\b(?:qDebug|qInfo|qWarning|qCritical|qFatal|fprintf|printf|'
                             r'std::cerr|std::cout|std::clog|writeText)\s*\(')),
    ('selftest_out', re.compile(r'\b(?:fail|ok|check|expect|note|out|append|report)\s*\(')),
]
TAIL_PATTERNS = [
    ('object_name', re.compile(r'\b(?:setObjectName|setProperty|setRegisteredObjectName)\s*\(\s*$')),
    ('json_key',    re.compile(r'\b(?:value|insert|setValue|toVariant|fromVariant)\s*\(\s*$')),
]
CALL_SITE_WINDOW = 96


def _strip_prefix(tail: str) -> str:
    """Drop a u8/u/U/L literal prefix from the code tail."""
    return re.sub(r'[uUL]{1,2}8?$', '', tail)


def scan_cpp(text: str):
    """Lex one C++ file; yield (line, literal, ctx, prefix).

    `ctx` is the *statement prefix*: the code since the last `;`/`{`/`}` (at most
    CALL_SITE_WINDOW characters), with comments and earlier literals removed.
    Using the statement instead of the last 96 raw characters is what lets a
    literal that sits in the second argument of a UI call
    (`addCommand(id, QStringLiteral("&Close Pane"), ...)`) still be classified
    by that call.
    """
    out = []
    i, n, line = 0, len(text), 1
    stmt = ''
    while i < n:
        c = text[i]
        if c == '\n':
            line += 1
            i += 1
            continue
        if c == '/' and i + 1 < n and text[i + 1] == '/':
            j = text.find('\n', i)
            i = n if j < 0 else j
            continue
        if c == '/' and i + 1 < n and text[i + 1] == '*':
            j = text.find('*/', i + 2)
            if j < 0:
                break
            line += text.count('\n', i, j)
            i = j + 2
            continue
        if c == 'R' and i + 1 < n and text[i + 1] == '"':
            k = text.find('(', i + 2)
            if k < 0:
                i += 1
                continue
            delim = text[i + 2:k]
            end = text.find(')' + delim + '"', k + 1)
            if end < 0:
                break
            line += text.count('\n', i, end)
            i = end + len(delim) + 2
            stmt = (stmt + ' RSTR ')[-CALL_SITE_WINDOW:]
            continue
        if c == "'":
            # char literal, or a C++14 digit separator: skip to the closing '
            j = i + 1
            while j < n:
                if text[j] == '\\':
                    j += 2
                    continue
                if text[j] == "'":
                    j += 1
                    break
                if text[j] == '\n':
                    break
                j += 1
            i = j
            stmt = (stmt + ' CHR ')[-CALL_SITE_WINDOW:]
            continue
        if c == '"':
            start_line = line
            j = i + 1
            buf = []
            closed = False
            while j < n:
                d = text[j]
                if d == '\\' and j + 1 < n:
                    e = text[j + 1]
                    if e == '\n':
                        line += 1
                        j += 2
                        continue
                    buf.append({'"': '"', '\\': '\\', 'n': '\n', 't': '\t',
                                'r': '\r', '0': '\0'}.get(e, '\\' + e))
                    j += 2
                    continue
                if d == '"':
                    closed = True
                    break
                if d == '\n':
                    line += 1
                    buf.append('\n')
                    j += 1
                    continue
                buf.append(d)
                j += 1
            if not closed:
                break
            tail = _strip_prefix(stmt.rstrip())
            out.append((start_line, ''.join(buf), tail, ''))
            i = j + 1
            stmt = (stmt + ' LIT ')[-CALL_SITE_WINDOW:]
            continue
        if c in ';{}':
            stmt = ''
            i += 1
            continue
        stmt = (stmt + c)[-CALL_SITE_WINDOW:]
        i += 1
    return out


# Wrappers whose own name says nothing about *where* the text goes: peel them
# so `action->setText(QStringLiteral("x"))` reads as `ui_text`, not as a
# literal wrapper.
WRAPPER_RE = re.compile(r'(?:QStringLiteral|QLatin1String|QString::fromUtf8|'
                        r'QString::fromLatin1|QString|QCoreApplication::translate)\s*\(\s*$')
TR_TAIL_RE = re.compile(r'(?:\btr|QObject::tr)\s*\(\s*$')


def classify_call_site(ctx: str) -> str:
    """Classify a literal by the enclosing statement.

    `ctx` is the statement prefix ending right before the literal, so a
    trailing `tr(` means the literal is already translated, a trailing
    `QStringLiteral(` is only a wrapper to peel, and any other call in the
    statement says where the value is going.
    """
    c = ctx.rstrip()
    if TR_TAIL_RE.search(c):
        return 'tr'
    for _ in range(3):
        m = WRAPPER_RE.search(c)
        if not m:
            break
        c = c[:m.start()].rstrip()
        if TR_TAIL_RE.search(c):
            return 'tr'
    for name, rx in TAIL_PATTERNS:
        if rx.search(c):
            return name
    for name, rx in ANY_PATTERNS:
        if rx.search(c):
            return name
    return 'other'


def is_path_like(s: str) -> bool:
    if re.search(r'\s', s):
        return False
    if '/' in s:
        parts = [p for p in s.split('/') if p not in ('', '.', '..')]
        if parts and all(PLAIN_SEGMENT_RE.match(p) for p in parts):
            return True
    if DATA_EXT_RE.search(s) and PLAIN_SEGMENT_RE.match(s):
        return True
    return False


def candidate_kind(s: str, ctx: str, path: str, keep_newlines: bool = False) -> tuple[bool, str]:
    """Return (is_candidate, drop_reason_if_not). Rules C1..C8."""
    if not s.strip():
        return False, 'C1-empty'
    stripped = s.strip()
    if stripped.startswith('#'):
        return False, 'C2-preprocessor'
    if is_path_like(stripped):
        return False, 'C3-path'
    if not re.search(r'[A-Za-z\u0080-\uffff]', stripped):
        if FORMAT_ONLY_RE.match(stripped) or re.fullmatch(r'%\d+|%[a-zA-Z]', stripped):
            return False, 'C4-format-only'
    if LOWERCASE_TOKEN_RE.match(stripped) and not re.search(r'[A-Z]', stripped):
        return False, 'C5-lowercase-token'
    if not re.search(r'[A-Za-z\u0080-\uffff]', s):
        return False, 'C6-no-word'
    if len(stripped) < 2:
        return False, 'C7-single-char'
    if not keep_newlines and ('\n' in s or '\r' in s):
        return False, 'C8-embedded-newline'
    return True, ''


def workspace_files(ref: str | None, include_headers: bool = False) -> list[str]:
    if ref:
        out = subprocess.run(['git', 'ls-tree', '-r', '--name-only', ref, '--', 'src'],
                             capture_output=True, text=True, check=True).stdout
        files = [f for f in out.split('\n') if f]
    else:
        files = []
        for root, _dirs, names in os.walk('src'):
            for n in names:
                files.append(os.path.join(root, n).replace(os.sep, '/'))
    exts = SOURCE_EXTS if include_headers else tuple(e for e in SOURCE_EXTS if e != '.h')
    return sorted(f for f in files if f.endswith(exts))


def read_source(path: str, ref: str | None) -> str | None:
    if ref:
        r = subprocess.run(['git', 'show', f'{ref}:{path}'], capture_output=True, text=True)
        return r.stdout if r.returncode == 0 else None
    try:
        with open(path, encoding='utf-8') as fh:
            return fh.read()
    except OSError:
        return None


def tree_state(ref: str | None) -> dict:
    def git(*args):
        try:
            return subprocess.run(['git', *args], capture_output=True, text=True,
                                  check=True).stdout.strip()
        except (subprocess.CalledProcessError, FileNotFoundError):
            return 'unknown'

    head = git('rev-parse', 'HEAD')
    dirty = 0
    if ref is None:
        status = git('status', '--porcelain')
        dirty = len([l for l in status.split('\n') if l]) if status != 'unknown' else -1
    return {
        'collected_at': _dt.datetime.now().astimezone().isoformat(timespec='seconds'),
        'collected_at_utc': _dt.datetime.now(_dt.timezone.utc).isoformat(timespec='seconds'),
        'extraction_source': f'git ref {ref}' if ref else 'working tree',
        'git_head': head if ref is None else git('rev-parse', ref),
        'dirty_paths': dirty,
        'rules_version': RULES_VERSION,
    }


def sha256_of(data: str) -> str:
    return hashlib.sha256(data.encode('utf-8')).hexdigest()[:16]


def collect(ref: str | None, scope: str = 'qstringliteral', keep_newlines: bool = False,
            include_headers: bool = False, require_whitespace: bool = False):
    rows = []
    skipped = []
    manifest = {}
    for path in workspace_files(ref, include_headers):
        text = read_source(path, ref)
        if text is None:
            skipped.append((path, 'unreadable'))
            continue
        manifest[path] = sha256_of(text)
        for (line, lit, ctx, prefix) in scan_cpp(text):
            site = classify_call_site(ctx)
            ok, reason = candidate_kind(lit, ctx, path, keep_newlines)
            if ok and scope == 'qstringliteral' and site != 'qstringliteral':
                ok, reason = False, 'C9-not-qstringliteral'
            if ok and require_whitespace and not re.search(r'\s', lit):
                ok, reason = False, 'C10-no-whitespace'
            rows.append({
                'source': f'{path}:{line}',
                'path': path,
                'line': line,
                'string': lit,
                'call_site': site,
                'context': ctx,
                'prefix': prefix,
                'candidate': ok,
                'drop_reason': reason,
            })
    return rows, skipped, manifest


def write_log(rows, state, out):
    cands = [r for r in rows if r['candidate']]
    uniq = len({r['string'] for r in cands})
    lines = [
        '# C9 evidence header (regenerated, reproducibly)',
        f"collected_at: {state['collected_at']}",
        f"collected_at_utc: {state['collected_at_utc']}",
        f"git_head: {state['git_head']}",
        f"extraction_source: {state['extraction_source']}",
        f"dirty_paths: {state['dirty_paths']}",
        f"rules_version: {state['rules_version']}",
        f"extractor: evidence/i18n/extract_i18n_candidates.py",
        '---',
        '## Candidate user-visible QStringLiteral strings '
        '(heuristic: shape rules C1..C7, see extractor docstring)',
        '',
        f'total_unique_candidates={uniq}',
        f'total_occurrences={len(cands)}',
        '',
    ]
    for r in cands:
        lines.append(f"{r['path']}:{r['line']}: {r['string']}")
    text = '\n'.join(lines) + '\n'
    _emit(text, out)


def write_csv(rows_all, out):
    fields = ['source', 'path', 'line', 'string', 'candidate', 'drop_reason',
              'call_site', 'context', 'prefix']
    _buf = []
    for r in rows_all:
        _buf.append({k: r[k] for k in fields})
    if out in (None, '-', '/dev/stdout'):
        w = csv.DictWriter(sys.stdout, fieldnames=fields, lineterminator='\n')
        w.writeheader()
        w.writerows(_buf)
        return
    with open(out, 'w', newline='', encoding='utf-8') as fh:
        w = csv.DictWriter(fh, fieldnames=fields, lineterminator='\n')
        w.writeheader()
        w.writerows(_buf)


def write_json(rows_all, state, manifest, out):
    doc = {
        'header': state,
        'scanned_files': len(manifest),
        'manifest_sha256_16': manifest,
        'rows': rows_all,
    }
    _emit(json.dumps(doc, ensure_ascii=False, indent=1) + '\n', out)


def _emit(text, out):
    if out in (None, '-', '/dev/stdout'):
        sys.stdout.write(text)
    else:
        with open(out, 'w', encoding='utf-8') as fh:
            fh.write(text)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[1])
    ap.add_argument('--ref', default=None,
                    help='git ref to extract from (default: working tree)')
    ap.add_argument('--out', default=None,
                    help=f'output path; "-" is stdout (default log: {DEFAULT_OUT})')
    ap.add_argument('--format', choices=('log', 'csv', 'json'), default='log')
    ap.add_argument('--profile', choices=('strict', 'broad'), default='strict',
                    help="strict (default): .cpp + QStringLiteral + whitespace present = the 01 "
                         "inventory's family; broad: also single-word labels, headers, newline strings")
    ap.add_argument('--any-literal', action='store_true',
                    help="candidate scope: any string literal (default: only literals directly "
                         "wrapped by QStringLiteral(), the 01 inventory's family)")
    ap.add_argument('--keep-newlines', action='store_true',
                    help='keep literals that contain an embedded newline (rule C8 off)')
    ap.add_argument('--all', action='store_true',
                    help='emit every literal (candidates and rejected) with drop reasons')
    ap.add_argument('--quiet', action='store_true', help='no summary on stderr')
    args = ap.parse_args(argv)

    scope = 'any' if args.any_literal else 'qstringliteral'
    strict = args.profile == 'strict'
    rows, skipped, manifest = collect(args.ref, scope, args.keep_newlines,
                                      include_headers=not strict,
                                      require_whitespace=strict and not args.keep_newlines)
    state = tree_state(args.ref)
    state['scanned_files'] = len(manifest)
    out = args.out
    if args.format == 'log':
        if out is None:
            out = DEFAULT_OUT
        write_log(rows, state, out)
    elif args.format == 'csv':
        write_csv(rows if args.all else [r for r in rows if r['candidate']], out)
    else:
        write_json(rows if args.all else [r for r in rows if r['candidate']],
                   state, manifest, out)

    if not args.quiet:
        cand = [r for r in rows if r['candidate']]
        dropped = collections.Counter(r['drop_reason'] for r in rows if not r['candidate'])
        sites = collections.Counter(r['call_site'] for r in cand)
        print(f'[{RULES_VERSION}] scanned={len(manifest)} literals={len(rows)} '
              f'candidates={len(cand)} unique={len({r["string"] for r in cand})} '
              f'skipped_files={len(skipped)}', file=sys.stderr)
        print('  dropped by rule: ' + ', '.join(f'{k}={v}' for k, v in sorted(dropped.items())),
              file=sys.stderr)
        print('  candidate call-sites: ' + ', '.join(f'{k}={v}' for k, v in sites.most_common()),
              file=sys.stderr)
    return 0


if __name__ == '__main__':
    sys.exit(main())
