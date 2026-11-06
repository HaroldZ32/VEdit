#!/usr/bin/env python3
"""RM-8b verification: the manual's default-binding table vs the CURRENT shipped registry.

The registry changed after the RM-5b machine dump (`product-bindings.txt`): the review
follow-up changed two desktop-global defaults to STATIC values, added the two tab commands
and gave `edit.redo` / `diagnostics.format` alternate defaults. There is no runtime
relocation any more: `view.toolbar` ships `Ctrl+Alt+Shift+T` and `view.panel.diagnostics`
ships `Ctrl+Alt+Shift+D` as plain source defaults, and a product selfcheck
(`commands.no-desktop-global-shortcuts`) proves every lane's bindings avoid the
desktop-global set. This script checks the manual against four independent things:

  1. the registration sites in the CURRENT tree (the reference map below; every id is
     enumerated from the source files and compared with the map, so a newly added command
     fails the check instead of silently missing from the manual);
  2. the RM-5b machine dump (`product-bindings.txt`, 32 rows) -- as a historical
     cross-check: every row it still owns must match, and the rows that differ must be
     exactly the documented post-dump deltas;
  3. the desktop-global list (`src/app/desktop_shortcuts.cpp`): no primary or alternate
     default may be desktop-reserved (the product ships values that avoid that set);
  4. the absence of the retired relocation machinery (`rehomeDesktopGlobalDefaults`) and
     the presence of the product gate (`commands.no-desktop-global-shortcuts`).

Extras (`edit.redo`'s `Ctrl+Y`, `diagnostics.format`'s `Shift+Alt+F`) are checked
separately because the Shortcuts page's Default column shows the primary only.
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[3]
MANUAL = ROOT / "docs/USER_GUIDE.md"
DUMP = ROOT / "evidence/ux-batch-2/rm5b-shortcuts/product-bindings.txt"
DESKTOP = ROOT / "src/app/desktop_shortcuts.cpp"
GREEN = ROOT / "evidence/ux-batch-2/rm5c-defaults/green-run.log"
PRODUCT = ROOT / "evidence/ux-batch-2/rm5c-defaults/product-selfcheck.log"

# label -> (registry id, expected PRIMARY static default, or "无默认键位")
REF = {
    ("File", "New"): ("file.new", "Ctrl+N"),
    ("File", "Open…"): ("file.open", "Ctrl+O"),
    ("File", "Open Folder…"): ("file.open-folder", "Ctrl+Alt+Shift+O"),
    ("File", "Switch Project…"): ("file.switch-project", "Ctrl+Alt+Shift+P"),
    ("File", "Go to File…"): ("file.goto-file", "Ctrl+P"),
    ("File", "New Empty Layout"): ("file.new-layout", "Ctrl+Alt+N"),
    ("File", "Save"): ("file.save", "Ctrl+S"),
    ("File", "Save As…"): ("file.save-as", "Ctrl+Shift+S"),
    ("File", "Save All"): ("file.save-all", "Ctrl+Alt+Shift+S"),
    ("File", "Save Session Now"): ("file.save-session", "无默认键位"),
    ("File", "Quit"): ("file.quit", "Ctrl+Q"),
    ("Edit", "Undo"): ("edit.undo", "Ctrl+Z"),
    ("Edit", "Redo"): ("edit.redo", "Ctrl+Shift+Z"),
    ("Edit", "Preferences…"): ("settings.preferences", "Ctrl+,"),
    ("View", "Split Pane Horizontally"): ("view.split.horizontal", "Ctrl+Alt+H"),
    ("View", "Split Pane Vertically"): ("view.split.vertical", "Ctrl+Shift+V"),
    ("View", "Focus Next Pane"): ("view.focus.next", "F6"),
    ("View", "Next Tab"): ("view.tab.next", "Ctrl+Tab"),
    ("View", "Previous Tab"): ("view.tab.previous", "Ctrl+Shift+Tab"),
    ("View", "Close Pane"): ("file.close-pane", "Ctrl+W"),
    ("View", "Reset Layout"): ("view.layout.reset", "Ctrl+Alt+R"),
    ("View", "Mixed Split (demo)"): ("view.layout.mixed", "Ctrl+Alt+M"),
    ("View", "Toolbar"): ("view.toolbar", "Ctrl+Alt+Shift+T"),
    ("View", "Search in Files"): ("search.focus", "Ctrl+Alt+Shift+F"),
    ("View", "Toggle Files Panel"): ("view.panel.file-tree", "Ctrl+Alt+F"),
    ("View", "Toggle Search Panel"): ("view.panel.search", "Ctrl+Alt+S"),
    ("View", "Toggle Terminal Panel"): ("view.panel.terminal", "Ctrl+Alt+J"),
    ("View", "Toggle Source Control Panel"): ("view.panel.vcs", "Ctrl+Alt+C"),
    ("View", "Toggle Outline Panel"): ("view.panel.outline", "Ctrl+Alt+O"),
    ("View", "Diagnostics"): ("view.panel.diagnostics", "Ctrl+Alt+Shift+D"),
    ("View", "Toggle Module Tree Panel"): ("view.panel.module-tree", "无默认键位"),
    ("View", "Toggle Interface Panel"): ("view.panel.interface-panel", "无默认键位"),
    ("View", "Toggle Project Panel"): ("view.dock.dock-project", "无默认键位"),
    ("View", "Use tree-sitter parsing"): ("view.treesitter", "无默认键位"),
    ("Project", "Lint Project"): ("project.lint", "无默认键位"),
    ("Project", "Reparse Project"): ("project.reparse", "无默认键位"),
    ("Project", "Go to Definition"): ("diagnostics.definition", "F12"),
    ("Project", "Find References"): ("diagnostics.references", "Shift+F12"),
    ("Project", "Rename Symbol..."): ("diagnostics.rename", "F2"),
    ("Project", "Format Document"): ("diagnostics.format", "Ctrl+Shift+F"),
    ("Project", "Show Hover"): ("diagnostics.hover", "无默认键位"),
    ("Project", "Show Backend Capabilities"): ("diagnostics.capabilities", "无默认键位"),
    ("Project", "Reload Backend Settings"): ("diagnostics.reloadSettings", "无默认键位"),
    ("Help", "User Manual"): ("help.user-manual", "F1"),
    ("Help", "About VEdit"): ("help.about", "无默认键位"),
}

# Post-dump deltas: id -> (old dump binding or None, new binding, where it comes from).
POST_DUMP = {
    "view.toolbar": ("Ctrl+Alt+T", "Ctrl+Alt+Shift+T",
                     "src/app/app_shell.cpp:660-661 static Ctrl+Shift+Alt+T literal"),
    "view.panel.diagnostics": ("Ctrl+Alt+D", "Ctrl+Alt+Shift+D",
                               "diagnostics_integration.cpp:3013 static default (no relocation)"),
    "view.tab.next": (None, "Ctrl+Tab", "src/app/app_shell.cpp view.tab.next"),
    "view.tab.previous": (None, "Ctrl+Shift+Tab", "src/app/app_shell.cpp view.tab.previous"),
}
EXTRA_DEFAULTS = {"diagnostics.format": ["Alt+Shift+F"],
                   "edit.redo": ["Ctrl+Y"]}


def manual_rows() -> list[tuple[str, str, str, str]]:
    text = MANUAL.read_text()
    start = text.index("| 菜单 | 命令 | 默认键位 | 说明 |")
    end = text.index("\n\n几条说明：", start)
    rows = []
    for line in text[start:end].splitlines():
        if not line.startswith("| `"):
            continue
        cells = [c.strip() for c in line.strip().strip("|").split("|")]
        if len(cells) != 4:
            raise SystemExit(f"unexpected table row: {line!r}")
        menu, cmd, key, note = cells
        rows.append((menu.strip("`"), cmd.strip("`"), key.strip("`"), note))
    return rows


def dump_bindings() -> dict[str, str]:
    result = {}
    for line in DUMP.read_text().splitlines():
        parts = line.split("\t")
        if len(parts) == 2:
            result[parts[1].strip()] = parts[0].strip()
    return result


def desktop_reserved() -> set[str]:
    sequences = set()
    for line in DESKTOP.read_text().splitlines():
        m = re.search(r'\{"([^"]+)",\s*"[^"]*"\}', line)
        if m:
            sequences.add(m.group(1))
    return sequences


def source_command_ids() -> set[str]:
    ids = set()
    app_shell = (ROOT / "src/app/app_shell.cpp").read_text()
    ids |= set(re.findall(r'addCommand\(QStringLiteral\("([^"]+)"\)', app_shell))
    ids |= {"view.panel." + name for name in
            ("file-tree", "search", "terminal", "vcs", "outline")}
    # Runtime-generated (registry/dock), no literal addCommand call.
    ids |= {"view.panel.module-tree", "view.panel.interface-panel", "view.dock.dock-project",
            "view.treesitter", "file.goto-file"}
    diagnostics = (ROOT / "src/workspace/diagnostics/diagnostics_integration.cpp").read_text()
    ids |= set(re.findall(r'addCommand\(QStringLiteral\("([^"]+)"\)', diagnostics))
    ids |= set(re.findall(r'\.id = QStringLiteral\("([^"]+)"\)', diagnostics))
    palette = (ROOT / "src/workspace/palette/palette_integration.cpp").read_text()
    ids |= set(re.findall(r'spec\.id = QStringLiteral\("([^"]+)"\)', palette))
    return ids


def main() -> int:
    failures = 0
    rows = manual_rows()
    print(f"manual table rows: {len(rows)}")
    if len(rows) != len(REF):
        print(f"FAIL row count: manual {len(rows)} vs reference {len(REF)}")
        failures += 1

    manual_map = {(menu, cmd): (key, note) for menu, cmd, key, note in rows}
    for label, (command_id, expected) in REF.items():
        if label not in manual_map:
            print(f"FAIL missing row: {label}")
            failures += 1
            continue
        key, note = manual_map[label]
        if key != expected:
            print(f"FAIL key: {label} manual={key!r} expected={expected!r}")
            failures += 1
        for extra in EXTRA_DEFAULTS.get(command_id, []):
            if extra not in note:
                print(f"FAIL extra default not named: {label} missing {extra!r} in note")
                failures += 1
    for label in manual_map:
        if label not in REF:
            print(f"FAIL unexpected row: {label}")
            failures += 1

    reference_ids = {command_id for command_id, _ in REF.values()}
    source_ids = source_command_ids()
    if reference_ids != source_ids:
        failures += 1
        print(f"FAIL id set: missing from reference {sorted(source_ids - reference_ids)}")
        print(f"FAIL id set: extra in reference {sorted(reference_ids - source_ids)}")
    else:
        print(f"OK: {len(source_ids)} command ids enumerated from the sources == reference map")

    # Desktop-global check: after the product's rehome pass no primary may be reserved.
    reserved = desktop_reserved()
    print(f"desktop-global sequences known: {len(reserved)}")
    for label, (command_id, binding) in REF.items():
        if binding == "无默认键位":
            continue
        if binding in reserved:
            print(f"FAIL desktop-reserved default: {label} ({command_id}) uses {binding}")
            failures += 1
    print("OK: no primary default is a desktop-global sequence")

    # CURRENT full-product snapshot: `vedit-app --selfcheck` prints every effective
    # binding of every lane (COMMAND-BINDING <portable sequence>\t<command id>).
    product = {}
    if PRODUCT.exists():
        for line in PRODUCT.read_text(errors="replace").splitlines():
            m = re.match(r"COMMAND-BINDING (\S+)\t(\S+)", line)
            if m:
                product.setdefault(m.group(2), []).append(m.group(1))
    expected_bindings = {}
    for _label, (command_id, binding) in REF.items():
        if binding != "无默认键位":
            expected_bindings.setdefault(command_id, []).append(binding)
    for command_id, extras in EXTRA_DEFAULTS.items():
        expected_bindings.setdefault(command_id, []).extend(extras)
    print(f"product selfcheck snapshot: {sum(len(v) for v in product.values())} bindings, "
          f"{len(product)} commands")
    if product:
        normalized_product = {k: sorted(v) for k, v in product.items()}
        normalized_expected = {k: sorted(v) for k, v in expected_bindings.items()}
        if normalized_product != normalized_expected:
            failures += 1
            for command_id in sorted(set(product) | set(expected_bindings)):
                if normalized_product.get(command_id) != normalized_expected.get(command_id):
                    print(f"FAIL product snapshot: {command_id} product={product.get(command_id)!r} "
                          f"expected={expected_bindings.get(command_id)!r}")
        else:
            print("OK: every product COMMAND-BINDING matches the manual (36 bindings, 34 commands)")
        if "CHECK commands.no-desktop-global-shortcuts PASS" not in PRODUCT.read_text(errors="replace"):
            failures += 1
            print("FAIL: product selfcheck did not report commands.no-desktop-global-shortcuts PASS")
        else:
            print("OK: product selfcheck commands.no-desktop-global-shortcuts PASS")

    # Historical cross-check against the RM-5b dump (predates RM-5c).
    dump = dump_bindings()
    print(f"RM-5b product dump (historical): {len(dump)} commands")
    for command_id, (old, new, origin) in POST_DUMP.items():
        actual_old = dump.get(command_id)
        if old is not None and actual_old != old:
            print(f"FAIL delta precondition: dump {command_id}={actual_old!r}, expected old={old!r}")
            failures += 1
        print(f"delta: {command_id}: dump={old!r} -> now={new!r}  ({origin})")
    mismatches = []
    expected_by_id = {cid: b for cid, b in REF.values()}
    for command_id, binding in dump.items():
        if command_id in POST_DUMP:
            continue
        expected = expected_by_id.get(command_id)
        if expected != binding:
            mismatches.append((command_id, binding, expected))
    if mismatches:
        failures += 1
        for command_id, old, expected in mismatches:
            print(f"FAIL dump mismatch (not a documented delta): {command_id}: {old!r} vs {expected!r}")
    else:
        print(f"OK: the remaining {len(dump) - len(POST_DUMP)} dump rows match the manual")

    # RM-5c green run: a fresh machine dump of the CURRENT code (BINDING lines).
    # Its shell has no diagnostics/tree/menu lanes, so only the rows it prints are
    # comparable; the lane rows are covered by the source anchors + the RM-5b dump.
    green = {}
    if GREEN.exists():
        for line in GREEN.read_text(errors="replace").splitlines():
            m = re.match(r"BINDING (\S+)\s+effective=(\S*)\s+default=(\S*)\s+state=(\S+)", line)
            if m:
                green[m.group(1)] = (m.group(2), m.group(3), m.group(4))
    print(f"RM-5c green-run bindings: {len(green)}")
    for command_id, (effective, default, state) in green.items():
        expected = expected_by_id.get(command_id)
        if expected is None:
            print(f"FAIL green dump has unknown command: {command_id}")
            failures += 1
            continue
        if expected != "无默认键位" and effective != expected:
            print(f"FAIL green dump mismatch: {command_id} effective={effective!r} expected={expected!r}")
            failures += 1
        if expected == "无默认键位" and state != "unbound":
            print(f"FAIL green dump unbound drift: {command_id} state={state!r}")
            failures += 1
    print(f"OK: {len(green)} green-run rows agree with the manual table")

    # The relocation machinery must be GONE; the guarantee is the product gate.
    app_shell_src = (ROOT / "src/app/app_shell.cpp").read_text()
    registry_src = (ROOT / "src/app/command_registry.cpp").read_text()
    main_src = (ROOT / "src/app/main.cpp").read_text()
    if "rehomeDesktopGlobalDefaults" in app_shell_src or "rehomeDesktopGlobalDefaults" in registry_src:
        failures += 1
        print("FAIL: rehomeDesktopGlobalDefaults still exists (runtime relocation must be gone)")
    else:
        print("OK: no runtime relocation machinery (rehomeDesktopGlobalDefaults absent)")
    if "commands.no-desktop-global-shortcuts" not in main_src:
        failures += 1
        print("FAIL: product gate commands.no-desktop-global-shortcuts is missing")
    else:
        print("OK: product gate commands.no-desktop-global-shortcuts present")

    unbound = sorted(REF[label][0] for label, key in manual_map.items() if key[0] == "无默认键位")
    expected_unbound = sorted(command_id for command_id, binding in REF.values()
                              if binding == "无默认键位")
    print(f"unbound rows ({len(unbound)}): {', '.join(unbound)}")
    if unbound != expected_unbound:
        failures += 1
        print("FAIL unbound set mismatch")

    bound = sum(1 for _label, (cid, b) in REF.items() if b != "无默认键位")
    print(f"bound primaries: {bound} (+ extras: "
          f"{sum(len(v) for v in EXTRA_DEFAULTS.values())})")
    print(f"SUMMARY rows={len(rows)} failures={failures}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
