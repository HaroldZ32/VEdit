#!/usr/bin/env python3
"""Add the T40 (i18n-l2 repair) messages to the hand-maintained .ts.

T40 wraps three groups of strings that were still English under zh_CN:

  * F1 — the diagnostics-panel status line (`%1 — %2 diagnostic(s)` and the two
    fragments of its sibling composition, `pending` / `saved file`);
  * F2 — the project dock title (`Project`) and the panel header composition
    (`Project: %1 — %2 %3` plus the `file`/`files` nouns and the ` · …` suffixes);
  * F4 — 20 of the 21 batch-2 "transient status-bar" strings. One string
    (`severity=%9 message='%10' file='%11'` in `lsp_actions.cpp`) is a developer
    log line, not a user sentence, and stays untranslated with a named reason in
    the reconciliation table.

Every message goes into the `VEdit` context with a real `<location>`, because the
wrap uses `QCoreApplication::translate("VEdit", …)` (the convention this file
already uses for the same class of strings). Translations carry no `type` and are
finished, so `lrelease` keeps reporting 0 unfinished.

The script is idempotent: a message whose `<source>` is already present in the
context is skipped, so a re-run after a partial edit cannot duplicate it.

Usage: python3 evidence/i18n-l2/tools/add_t40_messages.py [--check]
  --check   do not write; report what would be added.
"""
from __future__ import annotations

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[3]
TS = ROOT / "translations/vedit_zh_CN.ts"
CONTEXT = "VEdit"

# (source file relative to the workspace root, English source, Chinese translation)
MESSAGES: list[tuple[str, str, str]] = [
    # F1 — DiagnosticsPanel status line (and its two sibling fragments).
    ("src/workspace/diagnostics/diagnostics_panel.cpp", "pending", "待分析"),
    ("src/workspace/diagnostics/diagnostics_panel.cpp", "saved file", "已保存的文件"),
    (
        "src/workspace/diagnostics/diagnostics_panel.cpp",
        "%1 — %2 diagnostic(s)",
        "%1 — %2 条诊断",
    ),
    # F2 — project dock title + panel header composition.
    ("src/workspace/project/project_integration.cpp", "Project", "工程"),
    ("src/workspace/project/project_model.cpp", "Project: %1 — %2 %3", "工程：%1 — %2 %3"),
    ("src/workspace/project/project_model.cpp", "file", "个文件"),
    ("src/workspace/project/project_model.cpp", "files", "个文件"),
    ("src/workspace/project/project_model.cpp", " · %1 filelist", " · %1 个文件列表"),
    ("src/workspace/project/project_model.cpp", " · %1 filelists", " · %1 个文件列表"),
    ("src/workspace/project/project_model.cpp", " · %1 variable", " · %1 个变量"),
    ("src/workspace/project/project_model.cpp", " · %1 variables", " · %1 个变量"),
    ("src/workspace/project/project_model.cpp", " · %1 line skipped", " · 跳过 %1 行"),
    ("src/workspace/project/project_model.cpp", " · %1 lines skipped", " · 跳过 %1 行"),
    ("src/workspace/project/project_model.cpp", " · %1 unresolved", " · %1 个未解析变量"),
    # F4 — transient status-bar messages that are user sentences.
    (
        "src/app/app_shell.cpp",
        "Session could not be restored (%1) — starting clean",
        "会话无法恢复（%1）— 已从干净状态启动",
    ),
    ("src/app/app_shell.cpp", "Saved", "已保存"),
    ("src/app/app_shell.cpp", "Saved %1", "已保存 %1"),
    ("src/app/app_shell.cpp", "no project", "无工程"),
    ("src/editor/kernel/lsp_actions.cpp", "No diagnostics in this file", "此文件没有诊断"),
    (
        "src/workspace/diagnostics/diagnostics_integration.cpp",
        "Backend switch failed: %1",
        "后端切换失败：%1",
    ),
    (
        "src/workspace/diagnostics/diagnostics_integration.cpp",
        "%1 (%2) — %3 action(s)",
        "%1（%2）— %3 个动作",
    ),
    ("src/workspace/diagnostics/diagnostics_integration.cpp", "no table", "无能力表"),
    ("src/workspace/diagnostics/diagnostics_integration.cpp", "unknown basis", "未知依据"),
    (
        "src/workspace/diagnostics/diagnostics_integration.cpp",
        "Hover needs a KTextEditor view",
        "悬停信息需要 KTextEditor 视图",
    ),
    (
        "src/workspace/diagnostics/diagnostics_integration.cpp",
        "Hover request refused by %1",
        "%1 拒绝了悬停请求",
    ),
    (
        "src/workspace/diagnostics/diagnostics_integration.cpp",
        "Formatting failed: %1",
        "格式化失败：%1",
    ),
    (
        "src/workspace/diagnostics/diagnostics_integration.cpp",
        "Formatted with verible-verilog-format",
        "已用 verible-verilog-format 格式化",
    ),
    (
        "src/workspace/diagnostics/diagnostics_integration.cpp",
        "Already formatted (verible-verilog-format)",
        "已是格式化状态（verible-verilog-format）",
    ),
    (
        "src/workspace/diagnostics/diagnostics_integration.cpp",
        "No project file set — open a folder first",
        "没有工程文件集 — 请先打开一个文件夹",
    ),
    (
        "src/workspace/diagnostics/diagnostics_integration.cpp",
        "Project lint did not start: %1",
        "工程级 lint 未启动：%1",
    ),
    (
        "src/workspace/diagnostics/diagnostics_integration.cpp",
        "Linting project with %1…",
        "正在用 %1 做工程级 lint…",
    ),
    (
        "src/workspace/diagnostics/diagnostics_integration.cpp",
        "%1 needs a KTextEditor view",
        "%1 需要 KTextEditor 视图",
    ),
    (
        "src/workspace/diagnostics/diagnostics_integration.cpp",
        "No hover from %1 here",
        "%1 在这里没有悬停信息",
    ),
    (
        "src/workspace/diagnostics/diagnostics_integration.cpp",
        "Rename preview: %1 edit(s) from %2",
        "重命名预览：来自 %2 的 %1 处修改",
    ),
    (
        "src/workspace/diagnostics/diagnostics_integration.cpp",
        "No definition found",
        "未找到定义",
    ),
    (
        "src/workspace/diagnostics/diagnostics_integration.cpp",
        "%1 reference(s) from %2",
        "来自 %2 的 %1 处引用",
    ),
    # F3 audit (the 5 rows the verification turned red): user-visible text that
    # T38's table had claimed wrapped+translated. They are user sentences, so the
    # F4 rule applies — translated, not exempted.
    ("src/workspace/filetree/filetree_integration.cpp", "Opened %1", "已打开 %1"),
    (
        "src/workspace/settings/settings_panel.cpp",
        "Settings loaded from %1 with %2 warning(s); the built-in "
        "defaults were kept where the file is invalid.",
        "已从 %1 载入设置（%2 个警告）；文件无效处的设置已保留内置默认值。",
    ),
    (
        "src/workspace/settings/settings_panel.cpp",
        "%1 configuration problem(s) — the built-in defaults were kept "
        "where needed:",
        "%1 个配置问题 — 需要时已保留内置默认值：",
    ),
]


def xml_escape(text: str) -> str:
    return text.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


# Messages written by an earlier revision of this repair and no longer produced
# by any call site (the plural `%2` templates were replaced by explicit
# singular/plural messages, so the Chinese translation never needs a dummy
# second argument — QString::arg would otherwise warn "Argument missing").
STALE_SOURCES = [
    " · %1 filelist%2",
    " · %1 variable%2",
    " · %1 line%2 skipped",
]

# Strings this repair translated that the product no longer contains at all (the
# T40-era `Rename preview: %1 edit(s) from %2` was dropped from
# diagnostics_integration.cpp afterwards). Their .ts blocks are `type="vanished"`
# and must stay that way: there is no call site left to translate. They stay in
# MESSAGES because make_reconciliation.py uses that list as the T40 supplement
# set (removing an entry would shrink the 319-row table).
GONE_SOURCES = [
    "Rename preview: %1 edit(s) from %2",
]


def drop_stale(lines: list[str], start: int, end: int) -> tuple[list[str], int]:
    """Remove `<message>` blocks whose <source> is in STALE_SOURCES."""
    kept: list[str] = []
    index = start
    dropped = 0
    while index < end:
        line = lines[index]
        if line.strip() == "<message>":
            block_end = index
            while block_end < end and lines[block_end].strip() != "</message>":
                block_end += 1
            block = lines[index : block_end + 1]
            source = next(
                (re.sub(r"^\s*<source>|</source>\s*$", "", item) for item in block if "<source>" in item),
                "",
            )
            if source in STALE_SOURCES or source in GONE_SOURCES:
                dropped += 1
                index = block_end + 1
                continue
            kept.extend(block)
            index = block_end + 1
            continue
        kept.append(line)
        index += 1
    return kept, dropped


def refinish_vanished(
    lines: list[str], start: int, end: int, wanted: set[str]
) -> tuple[list[str], list[str]]:
    """Drop the `type="vanished"` copy of a message this list still needs (T27).

    lupdate only extracts a literal that is the DIRECT argument of
    `tr()`/`translate()`. The F2 suffixes are ternary branches
    (`translate("VEdit", lists == 1 ? " · %1 filelist" : " · %1 filelists")`), so a
    later `lupdate` run marks their existing messages `type="vanished"` and
    lrelease then stops emitting them — the panel header silently falls back to
    English. Removing the vanished block lets the normal add path below restore it
    with a `<location>`, so a re-run after any lupdate repairs the file.
    """
    kept: list[str] = []
    refinished: list[str] = []
    index = start
    while index < end:
        line = lines[index]
        if line.strip() == "<message>" and index > 0:
            block_end = index
            while block_end < end and lines[block_end].strip() != "</message>":
                block_end += 1
            block = lines[index : block_end + 1]
            source = next(
                (re.sub(r"^\s*<source>|</source>\s*$", "", item) for item in block if "<source>" in item),
                "",
            )
            vanished = any('type="vanished"' in item for item in block)
            if vanished and source in wanted:
                refinished.append(source)
                index = block_end + 1
                continue
            kept.extend(block)
            index = block_end + 1
            continue
        kept.append(line)
        index += 1
    return kept, refinished


def context_span(lines: list[str], name: str) -> tuple[int, int]:
    """Return (start, end) line indexes of the `<context>` block named `name`."""
    start = None
    for index, line in enumerate(lines):
        if line.strip() == "<context>":
            start = index
        elif start is not None and line.strip() == "</context>":
            block = "\n".join(lines[start:index])
            if f"<name>{name}</name>" in block:
                return start, index
            start = None
    raise SystemExit(f"FATAL: context {name!r} not found")


def find_line(path: pathlib.Path, literal: str) -> int:
    """The 1-based line of the wrapped literal (prefer a translate() line).

    A message may be a multi-line C concatenation (`"first part " "second part"`),
    so when the full literal is not on one line the search falls back to prefixes
    of the literal — the `<location>` only has to point at the statement.
    """
    lines = path.read_text(encoding="utf-8").splitlines()
    candidates = ['"' + literal + '"']
    for length in (120, 80, 60, 40):
        if len(literal) > length:
            candidates.append('"' + literal[:length])
    for wanted in candidates:
        fallback = 0
        for index, line in enumerate(lines, 1):
            if wanted not in line:
                continue
            if "translate(" in line or re.search(r"\btr\(", line):
                return index
            fallback = fallback or index
        if fallback:
            return fallback
    return 0


def main() -> int:
    check = "--check" in sys.argv[1:]
    lines = TS.read_text(encoding="utf-8").splitlines()
    lines, stale = drop_stale(lines, 0, len(lines))
    # A message this list still needs but a later lupdate marked vanished (ternary
    # branches are invisible to the extractor) is removed here and re-added below.
    lines, refinished = refinish_vanished(
        lines, 0, len(lines), {xml_escape(literal) for _, literal, _ in MESSAGES}
    )
    start, end = context_span(lines, CONTEXT)
    block = "\n".join(lines[start:end])
    sources = re.findall(r"<source>(.*?)</source>", block, re.S)

    added: list[str] = []
    skipped: list[str] = []
    rendered: list[str] = []
    gone: list[str] = []
    for relative, literal, translation in MESSAGES:
        escaped = xml_escape(literal)
        if escaped in GONE_SOURCES:
            gone.append(literal)
            continue
        if escaped in sources:
            skipped.append(literal)
            continue
        line = find_line(ROOT / relative, literal)
        if line == 0:
            raise SystemExit(f"FATAL: {literal!r} not found in {relative}")
        rendered.append(
            f"        <message>\n"
            f'            <location filename="../{relative}" line="{line}" />\n'
            f"            <source>{escaped}</source>\n"
            f"            <translation>{xml_escape(translation)}</translation>\n"
            f"        </message>"
        )
        added.append(literal)

    print(
        f"context={CONTEXT} existing={len(sources)} add={len(added)} skip={len(skipped)} "
        f"stale={stale} refinished={len(refinished)}"
    )
    for literal in refinished:
        print(f"  ~ {literal} (was type=\"vanished\"; restored)")
    for literal in added:
        print(f"  + {literal}")
    for literal in gone:
        print(f"  - {literal} (no call site left; .ts block stays vanished)")
    for literal in skipped:
        print(f"  = {literal} (already present)")
    if check:
        return 0
    if not rendered and not stale and not refinished:
        print("nothing to write")
        return 0
    lines[end:end] = "\n".join(rendered).splitlines()
    TS.write_text("\n".join(lines) + "\n", encoding="utf-8")
    total = len(re.findall(r"<message>", TS.read_text(encoding="utf-8")))
    print(f"wrote {TS.relative_to(ROOT)}; messages now {total}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
