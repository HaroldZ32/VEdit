#!/usr/bin/env python3
"""Dump the fold-related lines of the Verilog/SystemVerilog definitions embedded
in a distro libKF6SyntaxHighlighting.so (QRC_SYNTAX=ON). Used by AMENDMENTS rev2
C15 item 4 to compare the supported fold idiom with VEdit's own XML.

Usage: python3 extract-builtin-folding.py /usr/lib/x86_64-linux-gnu/libKF6SyntaxHighlighting.so.6.24.0
"""
import re
import sys

import zstandard

ZSTD_MAGIC = bytes.fromhex("28b52ffd")

def main(path: str) -> int:
    data = open(path, "rb").read()
    pos = 0
    hits = 0
    while True:
        pos = data.find(ZSTD_MAGIC, pos)
        if pos < 0:
            break
        start = pos
        pos += 4
        try:
            out = zstandard.ZstdDecompressor().decompress(
                data[start:start + 4_000_000], max_output_size=20_000_000
            )
        except Exception:
            continue
        if not out.lstrip().startswith(b"<?xml"):
            continue
        text = out.decode("utf-8", "replace")
        language = re.search(r"<language\b[^>]*>", text)
        if not language:
            continue
        tag = language.group(0)
        if 'name="Verilog"' not in tag and 'name="SystemVerilog"' not in tag:
            continue
        hits += 1
        print("===", re.sub(r"\s+", " ", tag)[:260])
        for line in text.splitlines():
            if any(k in line for k in ("beginRegion", "endRegion", "<folding", "<list name=")):
                print(line.rstrip())
        print()
    print(f"definitions_matched={hits}")
    return 0 if hits else 1

if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1] if len(sys.argv) > 1 else
                           "/usr/lib/x86_64-linux-gnu/libKF6SyntaxHighlighting.so.6.24.0"))
