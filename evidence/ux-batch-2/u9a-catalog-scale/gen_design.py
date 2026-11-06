#!/usr/bin/env python3
"""Generate a synthetic SystemVerilog project for the u9 slang-limit evidence.

Two knobs matter:
  * ``--files`` / ``--signals`` control how much *elaborated AST* slang produces
    (the module-catalog path);
  * ``--warnings`` adds width-truncation assignments, one warning each, which
    control how much *diagnostic JSON* the lint path produces.

The generator is deterministic: the same arguments produce the same tree.
"""
import argparse
import pathlib


def module_source(index: int, signals: int, warnings: int, segment: int) -> str:
    lines = [f"// synthetic module {index}"]
    has_child = index > 0 and (segment == 0 or index % segment != 0)
    child = f"  mod_{index - 1} u_child (.clk(clk), .din(din ^ 8'd1), .q());" if has_child else ""
    lines.append(
        f"module mod_{index} (input logic clk, input logic [7:0] din, output logic [7:0] q);"
    )
    lines.append("  logic [7:0] acc;")
    lines.append("  always_ff @(posedge clk) acc <= din;")
    lines.append("  assign q = acc;")
    for s in range(signals):
        lines.append(f"  logic [7:0] sig_{s};")
        lines.append(f"  assign sig_{s} = din ^ 8'd{s & 255};")
    # Each of these produces one `WidthTruncate` warning (8 -> 4 bits).
    for w in range(warnings):
        lines.append(f"  logic [3:0] narrow_{w};")
        lines.append(f"  assign narrow_{w} = din;")
    if child:
        lines.append(child)
    lines.append("endmodule")
    return "\n".join(lines) + "\n"


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--out", required=True)
    parser.add_argument("--files", type=int, default=1176)
    parser.add_argument("--signals", type=int, default=180)
    parser.add_argument("--warnings", type=int, default=0)
    parser.add_argument(
        "--segment",
        type=int,
        default=0,
        help="0 = every module is a design root; N>0 = instantiate the previous "
        "module except within each N-module segment (keeps elaboration depth < 128)",
    )
    args = parser.parse_args()

    root = pathlib.Path(args.out)
    root.mkdir(parents=True, exist_ok=True)
    for index in range(args.files):
        (root / f"mod_{index}.sv").write_text(
            module_source(index, args.signals, args.warnings, args.segment), encoding="utf-8"
        )
    print(f"wrote {args.files} files to {root}")


if __name__ == "__main__":
    main()
