#!/usr/bin/env python3
"""Independently re-check the rebuilt T62 evidence against the lock matrix.

T62 rebuilt 08-locked-package-versions.txt and 09-locked-deb-sha256.txt after the
evidence-loss incident. Both files embed their own SELF-CHECK lines, but a reader
should not have to trust them: this script re-derives the comparison from
  * scripts/docker/qt6-locked-packages.txt  (the lock matrix: pkg=version + deb sha256)
  * scripts/versions-qt.env                (the human-level version matrix)
and the two rebuilt evidence files.

Exit codes: 0 = everything consistent; 1 = a mismatch (details on stdout);
            2 = a required file is missing.
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
LOCK = ROOT / "scripts/docker/qt6-locked-packages.txt"
VERSIONS = ROOT / "scripts/versions-qt.env"
EV08 = ROOT / "evidence/m0-build/08-locked-package-versions.txt"
EV09 = ROOT / "evidence/m0-build/09-locked-deb-sha256.txt"


def fail(msg: str) -> None:
    print(f"MISMATCH: {msg}")
    raise SystemExit(1)


def main() -> int:
    for path in (LOCK, VERSIONS, EV08, EV09):
        if not path.is_file():
            print(f"MISSING: {path}", file=sys.stderr)
            return 2

    locked: dict[str, tuple[str, str]] = {}
    for line in LOCK.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        spec, sha = line.split()
        pkg, version = spec.split("=")
        locked[pkg] = (version, sha)

    ev08 = EV08.read_text(encoding="utf-8")
    ev09 = EV09.read_text(encoding="utf-8")

    # 1) every locked package must appear in 08 with OK and the locked version.
    for pkg, (version, _sha) in sorted(locked.items()):
        pattern = re.compile(rf"^{re.escape(pkg)}\s+(\S+)\s+\[lock {re.escape(version)}\] OK$", re.MULTILINE)
        match = pattern.search(ev08)
        if not match:
            fail(f"08: no OK line for {pkg} {version}")
        if match.group(1) != version:
            fail(f"08: installed version {match.group(1)} != locked {version} for {pkg}")
    print(f"OK: 08 lists all {len(locked)} locked packages with matching installed versions")

    # 2) every locked deb sha256 must appear in 09 with OK.
    for pkg, (_version, sha) in sorted(locked.items()):
        if not re.search(rf"\b{sha}\b OK$", ev09, re.MULTILINE):
            fail(f"09: no OK line with locked sha256 for {pkg}")
    print(f"OK: 09 verifies all {len(locked)} locked .deb sha256 values")

    # 3) the human-level matrix in versions-qt.env must be visible in 08.
    env = VERSIONS.read_text(encoding="utf-8")
    checks = [
        ("VEDIT_QT6_VERSION", r"qt6-base-dev\s+([^\s+]+)"),
        ("VEDIT_KF6_VERSION", r"libkf6texteditor-dev\s+([^\s-]+)"),
        ("VEDIT_TREE_SITTER_VERSION", r"libtree-sitter-dev\s+([^\s-]+)"),
        ("VEDIT_QT6_DISTRO_GLIBC", r"libc6\s+(\S+)"),
    ]
    for key, pattern in checks:
        want = re.search(rf'^{key}="([^"]+)"', env, re.MULTILINE)
        got = re.search(pattern, ev08)
        if not want or not got:
            fail(f"{key}: could not locate the value in versions-qt.env or in 08")
        if not got.group(1).startswith(want.group(1)):
            fail(f"{key}: versions-qt.env says {want.group(1)}, 08 shows {got.group(1)}")
        print(f"OK: {key}={want.group(1)} matches 08 ({got.group(1)})")

    print("RESULT: PASS — rebuilt 08/09 are consistent with the current lock matrix")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
