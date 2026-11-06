#!/usr/bin/env python3
"""Generate ONE SystemVerilog file whose slug lint floods diagnostics.

Each `assign narrow_i = din;` (8 -> 4 bits) is one `WidthTruncate` warning, so
`--warnings N` produces N findings in one file — the shape that used to exceed
the 8 MiB `--diag-json` cap and fail the FILE out of the project pass.
"""
import argparse
import pathlib


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--out", required=True)
    parser.add_argument("--warnings", type=int, default=40_000)
    args = parser.parse_args()

    lines = ["module flood (input logic [7:0] din, output logic [3:0] q);"]
    for index in range(args.warnings):
        lines.append(f"  logic [3:0] narrow_{index};")
        lines.append(f"  assign narrow_{index} = din;")
    lines.append("  assign q = din[3:0];")
    lines.append("endmodule")
    path = pathlib.Path(args.out)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"wrote {args.warnings} warnings to {path} ({path.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
