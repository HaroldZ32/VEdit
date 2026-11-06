#!/usr/bin/env python3
"""RM-8b verification: no listed default is a DEAD key (editor or desktop owns it).

Ownership sources, both shipped:
  * evidence/ux-batch-2/rm5b-shortcuts/probe/log-kte-action-dump.txt
        every QAction sequence the live KTextEditor view owns (114 actions);
  * src/app/desktop_shortcuts.cpp
        the well-known GNOME/KDE/VT global bindings.

The product's ownership authority (`ApplicationShell::syncOwnedShortcuts()` ->
`EditorArea::setOwnedShortcuts()` -> `clearOwnedShortcuts()`) removes every sequence the
registry owns from the editor's own actions, so an editor collision is not a dead key:
exactly one owner is left. The manual says so for the five affected keys. A desktop-global
sequence is different: the desktop consumes it before the process sees it, so the product
MOVES such a default at startup (`rehomeDesktopGlobalDefaults()`), and the manual lists the
moved value. This script asserts both invariants for the CURRENT table (45 commands,
34 primaries + 1 extra default).

The two sequences the earlier manual used that the editor owns are printed so the
regression behind their replacement stays visible (Ctrl+Shift+H, Ctrl+Alt+V).
"""
import pathlib
import re
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from verify_table import REF, EXTRA_DEFAULTS, desktop_reserved  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parents[3]
KTE = ROOT / "evidence/ux-batch-2/rm5b-shortcuts/probe/log-kte-action-dump.txt"

# Registry sequences the shell takes over from an editor action of the same name
# (asserted per command by the RM-5b key-path regression; see README §4.3).
TAKEN_OVER = {"Ctrl+Z", "Ctrl+Shift+Z", "Ctrl+S", "Ctrl+Shift+S", "Ctrl+P"}
AVOIDED = {"Ctrl+Shift+H", "Ctrl+Alt+V"}


def main() -> int:
    actions = []
    for line in KTE.read_text(errors="replace").splitlines():
        m = re.search(r"objectName=(.*?)\s+text=(.*?)\s+shortcuts=([^\s]+)", line)
        if m:
            actions.append((m.group(1), m.group(2), m.group(3).split(",")))
    print(f"KTextEditor actions: {len(actions)}")
    reserved = desktop_reserved()
    print(f"desktop-global sequences: {len(reserved)}")

    failures = 0
    collisions = set()
    checked = 0
    print()
    print(f'{"sequence":22s} {"command id":28s} {"editor owner(s)":50s} verdict')
    for label, (command_id, binding) in sorted(REF.items(), key=lambda kv: kv[1][0]):
        if binding == "无默认键位":
            continue
        checked += 1
        owners = [f"{obj} ({text})" for obj, text, seqs in actions if binding in seqs]
        if owners:
            collisions.add(binding)
            if binding not in TAKEN_OVER:
                verdict = "FAIL (editor owns it; shell does not take it over)"
                failures += 1
            else:
                verdict = "TAKEN OVER by the shell"
        else:
            verdict = "free"
        if binding in reserved:
            verdict += " / FAIL (desktop-global)"
            failures += 1
        print(f'{binding:22s} {command_id:28s} {", ".join(owners) if owners else "FREE":50s} {verdict}')

    for command_id, extras in EXTRA_DEFAULTS.items():
        for binding in extras:
            checked += 1
            owners = [f"{obj} ({text})" for obj, text, seqs in actions if binding in seqs]
            verdict = "free" if not owners else "FAIL (extra default collides with the editor)"
            if owners or binding in reserved:
                failures += 1
                verdict = "FAIL (extra default collides)"
            print(f'{binding:22s} {command_id + " (extra)":28s} {", ".join(owners) if owners else "FREE":50s} {verdict}')

    print()
    print(f"collisions: {len(collisions)} -> " + ", ".join(sorted(collisions)))
    print("all collisions are in the taken-over set: "
          + ("yes" if collisions <= TAKEN_OVER else "NO"))
    if collisions != TAKEN_OVER:
        failures += 1
        print(f"FAIL: taken-over set mismatch actual={sorted(collisions)} expected={sorted(TAKEN_OVER)}")

    print()
    print("sequences the OLD manual used, owned by the editor, now avoided:")
    for sequence in sorted(AVOIDED):
        owners = [f"{obj} ({text})" for obj, text, seqs in actions if sequence in seqs]
        print(f"  {sequence:16s} -> {', '.join(owners) if owners else 'FREE (unexpected)'}")
        if not owners:
            failures += 1

    print()
    print(f"SUMMARY checked={checked} failures={failures}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
