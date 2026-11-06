#!/usr/bin/env python3
"""Select the B-class batch-2 call sites (T38 / G7) from the CURRENT sources.

The wrap tool (`wrap_b_class.py`) needs the *concatenated, unescaped* literal
exactly as the compiler sees it. Grepping cannot produce that for adjacent or
multi-line literals, so this selector reuses the wrap tool's own lexer and
records, for each hand-picked (file, line, anchor) triple, the literal text of
the `QStringLiteral(...)` occurrence on that line whose text contains `anchor`.

Output: `evidence/i18n-l2/logs/18-b2-selected-rows.json` — appended to
`03-b-wrap-table.json` by the caller.
"""
from __future__ import annotations

import importlib.util
import json
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[3]
TOOL = ROOT / "evidence/i18n-l2/tools/wrap_b_class.py"
OUT = ROOT / "evidence/i18n-l2/logs/18-b2-selected-rows.json"

spec = importlib.util.spec_from_file_location("wrap_b_class", TOOL)
wrap = importlib.util.module_from_spec(spec)
assert spec.loader is not None
spec.loader.exec_module(wrap)

# (file, line, anchor substring of the literal). The anchor disambiguates two
# QStringLiteral calls on one line and guards against line drift.
SELECTION = [
    ("src/workspace/diagnostics/diagnostics_outline_model.cpp", 55, "Symbol"),
    ("src/workspace/diagnostics/diagnostics_outline_model.cpp", 57, "Detail"),
    ("src/workspace/diagnostics/diagnostics_outline_model.cpp", 59, "Line"),

    ("src/workspace/diagnostics/diagnostics_panel.cpp", 239, "no problems reported"),
    ("src/workspace/diagnostics/diagnostics_panel.cpp", 243, "open a file to start"),
    ("src/workspace/diagnostics/diagnostics_panel.cpp", 247, "No diagnostics"),
    ("src/workspace/diagnostics/diagnostics_panel.cpp", 250, "No diagnostics"),
    ("src/workspace/diagnostics/diagnostics_panel.cpp", 297, "analyzed"),
    ("src/workspace/diagnostics/diagnostics_panel.cpp", 317, "No symbols"),
    ("src/workspace/diagnostics/diagnostics_panel.cpp", 322, "No symbols yet"),
    ("src/workspace/diagnostics/diagnostics_panel.cpp", 327, "No symbols yet"),
    ("src/workspace/diagnostics/diagnostics_panel.cpp", 420, "%1:%2:%3"),

    ("src/workspace/module_tree/module_tree_integration.cpp", 135, "retrying"),
    ("src/workspace/module_tree/module_tree_integration.cpp", 171, "No hierarchy yet"),
    ("src/workspace/module_tree/module_tree_integration.cpp", 249, "Empty state"),

    ("src/workspace/module_tree/module_tree_panel.cpp", 45, "no file:line"),
    ("src/workspace/module_tree/module_tree_panel.cpp", 138, "No hierarchy yet"),
    ("src/workspace/module_tree/module_tree_panel.cpp", 148, "No hierarchy yet"),
    ("src/workspace/module_tree/module_tree_panel.cpp", 158, "Hierarchy ready"),

    ("src/workspace/module_tree/hierarchy_client.cpp", 265, "expanding the project"),
    ("src/workspace/module_tree/hierarchy_client.cpp", 313, "elaborating"),

    ("src/workspace/diagnostics/lint_progress.cpp", 98, "linting"),
    ("src/workspace/diagnostics/lint_progress.cpp", 98, "idle"),
    ("src/workspace/diagnostics/lint_progress.cpp", 111, "no request to re-run"),
    ("src/workspace/diagnostics/lint_progress.cpp", 176, "cannot re-run"),

    ("src/workspace/palette/command_palette.cpp", 438, "files · type"),
    ("src/workspace/palette/command_palette.cpp", 440, "of %2 files"),

    ("src/workspace/filetree/file_tree_widget.cpp", 182, "Opened"),
    ("src/workspace/filetree/file_tree_widget.cpp", 334, "Deleted"),
    ("src/workspace/filetree/file_tree_widget.cpp", 528, "Refreshed"),
    ("src/workspace/filetree/file_tree_widget.cpp", 563, "%1 - %2"),

    ("src/workspace/project/project_panel.cpp", 77, "Add one or more"),
    ("src/workspace/project/project_panel.cpp", 84, "Open a folder from the File Tree"),
    ("src/workspace/project/project_panel.cpp", 218, "Imported"),
    ("src/workspace/project/project_panel.cpp", 236, "Removed"),
    ("src/workspace/project/project_panel.cpp", 271, "unresolved filelist"),
    ("src/workspace/project/project_panel.cpp", 280, "Import completed"),
    ("src/workspace/project/project_panel.cpp", 289, "Saved"),
    ("src/workspace/project/project_panel.cpp", 312, "No variables are used"),
    ("src/workspace/project/project_panel.cpp", 322, "Project variables updated"),
    ("src/workspace/project/project_panel.cpp", 355, "Cannot open"),
    ("src/workspace/project/project_panel.cpp", 361, "Cannot open"),

    ("src/editor/kernel/editor_context_menu.cpp", 35, "Ask %1 for the definition"),
    ("src/editor/kernel/editor_context_menu.cpp", 37, "Ask %1 for the references"),
    ("src/editor/kernel/editor_context_menu.cpp", 40, "Ask %1 for a rename"),
    ("src/editor/kernel/editor_context_menu.cpp", 43, "Run the external"),
    ("src/editor/kernel/editor_context_menu.cpp", 46, "hover at the cursor"),
    ("src/editor/kernel/editor_context_menu.cpp", 50, "next diagnostic"),
    ("src/editor/kernel/editor_context_menu.cpp", 52, "previous diagnostic"),
    ("src/editor/kernel/editor_context_menu.cpp", 198, "Language Server"),
    ("src/editor/kernel/editor_context_menu.cpp", 210, "Diagnostics"),

    ("src/editor/kernel/editor_advanced.cpp", 78, "LSP Completion"),
    ("src/editor/kernel/editor_advanced.cpp", 80, "Keyword Completion"),
    ("src/editor/kernel/editor_advanced.cpp", 82, "Signature Help"),
    ("src/editor/kernel/editor_advanced.cpp", 84, "Code Actions"),
    ("src/editor/kernel/editor_advanced.cpp", 86, "Sticky Scroll"),
    ("src/editor/kernel/editor_advanced.cpp", 88, "Inlay Hints"),
    ("src/editor/kernel/editor_advanced.cpp", 90, "Snippets"),
    ("src/editor/kernel/editor_advanced.cpp", 92, "Indent Guides"),
    ("src/editor/kernel/editor_advanced.cpp", 94, "Show Whitespace"),
    ("src/editor/kernel/editor_advanced.cpp", 96, "Column Ruler"),
    ("src/editor/kernel/editor_advanced.cpp", 342, "available"),
    ("src/editor/kernel/editor_advanced.cpp", 352, "Module skeleton"),
    ("src/editor/kernel/editor_advanced.cpp", 353, "always_ff block"),
    ("src/editor/kernel/editor_advanced.cpp", 354, "case statement"),

    ("src/editor/kernel/fim_wiring.cpp", 55, "FIM ghost text"),

    ("src/workspace/project/project_variables_dialog.cpp", 28, "These variables"),

    ("src/workspace/settings/settings_panel.cpp", 702, "Theme"),
    ("src/workspace/settings/settings_panel.cpp", 770, "Shortcut for"),
    ("src/workspace/settings/settings_panel.cpp", 810, "Cancel to discard"),
    ("src/workspace/settings/settings_panel.cpp", 829, "could not"),

    ("src/workspace/diagnostics/diagnostics_integration.cpp", 1070, "LSP backend="),
]


def main() -> int:
    rows: list[dict] = []
    failures: list[str] = []
    cache: dict[str, list[dict]] = {}
    used: set[tuple[str, int]] = set()
    for path, line, anchor in SELECTION:
        if path not in cache:
            cache[path] = wrap.occurrences((ROOT / path).read_text(encoding="utf-8"))
        matches = sorted(
            (
                occurrence
                for occurrence in cache[path]
                if anchor in occurrence["literal"]
                and (path, occurrence["start"]) not in used
            ),
            key=lambda occurrence: abs(occurrence["line"] - line),
        )
        if not matches:
            failures.append(f"{path}:{line}: 0 unused occurrence(s) contain {anchor!r}")
            continue
        chosen = matches[0]
        used.add((path, chosen["start"]))
        rows.append(
            {"file": path, "line_hint": chosen["line"], "literal": chosen["literal"]}
        )
    if failures:
        print("SELECTION-FAILURES", file=sys.stderr)
        for failure in failures:
            print(f"  {failure}", file=sys.stderr)
        return 1
    OUT.write_text(json.dumps(rows, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"selected={len(rows)} -> {OUT.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
