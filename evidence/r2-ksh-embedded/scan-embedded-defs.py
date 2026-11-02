#!/usr/bin/env python3
"""Detect syntax definitions embedded inside libKF6SyntaxHighlighting.so.

WHY: KSyntaxHighlighting bundles its syntax definitions into the shared library
as Qt resources (`QRC_SYNTAX=ON`, the default). A file-level "don't copy the XML"
policy therefore does NOT prove the library is clean: the definitions ship inside
the .so, including GPL-family ones (SystemVerilog is `GPLv3+`).

HOW: the definitions are stored as zstd frames (pre-compressed by
`katehighlightingindexer`, then embedded by `rcc`). This tool
  1. scans the UTF-16LE resource-name table for `*.xml` / `*.theme` names,
  2. finds every zstd frame, decompresses it, and parses the `<language ...>` tag
     of each definition (name + license),
  3. classifies licenses into strict-GPL / LGPL / none / other,
  4. with `--expect-clean`, exits 1 when any strict-GPL definition is embedded.

Dependencies: python3 + `zstandard` (pip/apt `python3-zstandard`). Verified with
zstandard 0.25.0 on the probe host. No network access is used.

Usage:
  python3 scan-embedded-defs.py /usr/lib/x86_64-linux-gnu/libKF6SyntaxHighlighting.so.6.24.0
  python3 scan-embedded-defs.py --expect-clean "$PAYLOAD/lib/libKF6SyntaxHighlighting.so.6"
  python3 scan-embedded-defs.py --name SystemVerilog <lib.so>

Exit codes: 0 = scan completed and (with --expect-clean) no strict-GPL definition;
            1 = --expect-clean and at least one strict-GPL definition is embedded;
            2 = usage/dependency error.
"""
from __future__ import annotations

import argparse
import hashlib
import re
import sys
from collections import Counter

try:
    import zstandard as zstd
except ImportError:  # pragma: no cover - dependency error path
    sys.stderr.write(
        "ERROR: the `zstandard` python module is required (apt python3-zstandard, "
        "or pip install zstandard).\n"
    )
    raise SystemExit(2)

ZSTD_MAGIC = bytes.fromhex("28b52ffd")
XML_NAME_RE = re.compile(r"[A-Za-z0-9][A-Za-z0-9._+-]*\.xml")
THEME_NAME_RE = re.compile(r"[A-Za-z0-9][A-Za-z0-9._+-]*\.theme")
LANGUAGE_TAG_RE = re.compile(r"<language\b[^>]*>")
ATTR_RE = "{}=\"([^\"]*)\""


def resource_names(data: bytes) -> tuple[set[str], set[str]]:
    """UTF-16LE resource-name table (names may start at either byte alignment)."""
    xml: set[str] = set()
    theme: set[str] = set()
    for offset in (0, 1):
        text = data[offset:].decode("utf-16-le", "ignore")
        xml.update(XML_NAME_RE.findall(text))
        theme.update(THEME_NAME_RE.findall(text))
    return xml, theme


def embeddefs(data: bytes) -> list[dict]:
    """Decompress every zstd frame; keep the ones that are syntax definitions."""
    defs: list[dict] = []
    pos = 0
    while True:
        pos = data.find(ZSTD_MAGIC, pos)
        if pos < 0:
            break
        frame_start = pos
        pos += 4
        try:
            out = zstd.ZstdDecompressor().decompress(
                data[frame_start:frame_start + 4_000_000], max_output_size=20_000_000
            )
        except Exception:
            continue
        if not out.lstrip().startswith(b"<?xml"):
            continue
        head = out[:4000].decode("utf-8", "ignore")
        tag = LANGUAGE_TAG_RE.search(head)
        if not tag:
            continue
        tag_text = tag.group(0)
        name = re.search(ATTR_RE.format("name"), tag_text)
        lic = re.search(ATTR_RE.format("license"), tag_text)
        defs.append(
            {
                "offset": frame_start,
                "bytes": len(out),
                "sha256": hashlib.sha256(out).hexdigest(),
                "name": name.group(1) if name else None,
                "license": lic.group(1) if lic else "",
            }
        )
    return defs


def license_class(license_text: str) -> str:
    """'GPL' means strict/strong GPL (GPL, GPLv2+, GPLv3, 'GPL,BSD'); LGPL is separate."""
    text = license_text.strip()
    if re.search(r"LGPL", text, re.I) and not re.match(r"^GPL", text, re.I):
        return "LGPL"
    if re.search(r"GPL", text, re.I):
        return "GPL"
    if not text:
        return "none"
    return "other"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("library", help="path to libKF6SyntaxHighlighting.so*")
    ap.add_argument("--expect-clean", action="store_true",
                    help="fail if any strict-GPL definition is embedded")
    ap.add_argument("--name", help="print full metadata for one definition name")
    args = ap.parse_args()

    data = open(args.library, "rb").read()
    xml_names, theme_names = resource_names(data)
    defs = embeddefs(data)
    classes = Counter(license_class(d["license"]) for d in defs)
    gpl = [d for d in defs if license_class(d["license"]) == "GPL"]

    print(f"== embedded-definition scan ==")
    print(f"library   : {args.library}")
    print(f"size      : {len(data)} B")
    print(f"sha256    : {hashlib.sha256(data).hexdigest()}")
    print(f"resource names (utf-16le): xml={len(xml_names)} theme={len(theme_names)}")
    print(f"zstd frames parsed as definitions: {len(defs)}")
    print(f"license classes: {dict(sorted(classes.items()))}")
    print(f"strict-GPL definitions embedded: {len(gpl)}")
    for d in sorted(gpl, key=lambda x: (x["name"] or "")):
        print(f"  GPL  {d['name']} | license={d['license']!r} | {d['bytes']} B | sha256={d['sha256'][:16]}")
    for probe in ("SystemVerilog", "Verilog"):
        hit = [d for d in defs if d["name"] == probe]
        for d in hit:
            print(f"  INFO {probe}: license={d['license']!r} bytes={d['bytes']} sha256={d['sha256']}")
        if not hit:
            print(f"  INFO {probe}: not found among parsed definitions")

    if args.name:
        for d in defs:
            if d["name"] == args.name:
                print(f"== {args.name} ==")
                print(f"offset={d['offset']} bytes={d['bytes']} sha256={d['sha256']} "
                      f"license={d['license']!r}")

    if args.expect_clean:
        if gpl:
            print("RESULT: FAIL - strict-GPL syntax definitions are embedded in the library")
            return 1
        print("RESULT: PASS - no strict-GPL definition embedded")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
