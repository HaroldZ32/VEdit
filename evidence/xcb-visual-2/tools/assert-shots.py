#!/usr/bin/env python3
"""Independent (non-harness) assertions over every 7c screenshot.

The capture harnesses assert their own bytes/dimensions/colours; this script is
the SECOND, independent measurement used by the visual review. For each PNG it
records: byte size, exact dimensions, sha256, the number of distinct colours on a
5-pixel sample grid, and the share of the single most frequent sampled colour.

Pass rule (deliberately mechanical):
  * at least 4096 bytes, at least 8 distinct sampled colours, not blank
    (most frequent sampled colour <= 0.995 of the sample);
  * the shot is 1500x1000 (the requested size) or 1440x900 (the product's own
    `--screenshots` size), OR it is a modal dialog widget shot (dialog-*), which
    is intentionally dialog-sized;
  * a top-level shell grab that grew past 1000px height because of the shell's
    own minimum size is accepted and reported as a NOTE (never silently).

Usage: python3 evidence/xcb-visual-2/tools/assert-shots.py
Exit: 0 = every shot non-trivial; 1 = at least one failure.
"""
from __future__ import annotations

import hashlib
import sys
from collections import Counter
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[3]
SHOTS = ROOT / "evidence" / "xcb-visual-2" / "shots"
OUT = ROOT / "evidence" / "xcb-visual-2" / "logs" / "40-shot-assertions.txt"

EXACT_SIZES = {(1500, 1000), (1440, 900)}
MIN_BYTES = 4096
MIN_COLORS = 8
MAX_MODAL_SHARE = 0.995


def main() -> int:
    lines: list[str] = []
    notes: list[str] = []
    failures = 0
    pngs = sorted(SHOTS.rglob("*.png"))
    if not pngs:
        print("FATAL: no screenshots under", SHOTS)
        return 1

    lines.append("# independent screenshot assertions (bytes / dimensions / sha256 / colours)")
    lines.append(f"shots_dir: {SHOTS}")
    lines.append(f"count: {len(pngs)}")
    lines.append("")
    header = f"{'file':58s} {'bytes':>9s} {'size':>11s} {'colors':>7s} {'modal':>6s}  verdict"
    lines.append(header)
    lines.append("-" * len(header))

    for path in pngs:
        rel = str(path.relative_to(SHOTS))
        data = path.read_bytes()
        sha = hashlib.sha256(data).hexdigest()
        try:
            with Image.open(path) as img:
                img.load()
                width, height = img.size
                rgb = img.convert("RGB")
        except Exception as exc:  # unreadable image is a failure, not a crash
            lines.append(f"{rel:58s} {len(data):9d} {'-':>11s} {'-':>7s} {'-':>6s}  FAIL unreadable: {exc}")
            failures += 1
            continue

        px = rgb.load()
        sampled: Counter = Counter()
        for y in range(0, height, 5):
            for x in range(0, width, 5):
                sampled[px[x, y]] += 1
        total = sum(sampled.values())
        colors = len(sampled)
        modal = max(sampled.values()) / total if total else 1.0

        reasons: list[str] = []
        if len(data) < MIN_BYTES:
            reasons.append(f"bytes<{MIN_BYTES}")
        if colors < MIN_COLORS:
            reasons.append(f"colors<{MIN_COLORS}")
        if modal > MAX_MODAL_SHARE:
            reasons.append(f"blank(modal={modal:.3f})")

        is_dialog = "dialog" in Path(rel).name
        size_ok = (width, height) in EXACT_SIZES
        if not size_ok and not is_dialog:
            if width == 1500 and height > 1000:
                size_ok = True  # shell grew to its own minimum height
                notes.append(f"{rel}: {width}x{height} (>1500x1000 by the shell's minimum height)")
            elif abs(width - 1500) <= 4 and abs(height - 1000) <= 30:
                size_ok = True  # a widget grab rounded by the X11 frame/minimum size
                notes.append(f"{rel}: {width}x{height} (within 4x30px of the requested 1500x1000)")
            else:
                reasons.append("unexpected-size")

        verdict = "PASS" if not reasons else "FAIL " + ",".join(reasons)
        if reasons:
            failures += 1
        lines.append(
            f"{rel:58s} {len(data):9d} {width:5d}x{height:<5d} {colors:7d} {modal:6.3f}  {verdict}  sha256={sha}"
        )

    lines.append("")
    if notes:
        lines.append("## notes (accepted, recorded for the review)")
        lines.extend("NOTE " + note for note in notes)
        lines.append("")
    lines.append(f"checked={len(pngs)} failures={failures}")
    lines.append("RESULT " + ("PASS" if failures == 0 else "FAIL"))
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text("\n".join(lines) + "\n")
    print("\n".join(lines[-6:]))
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
