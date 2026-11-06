#!/usr/bin/env python3
"""Generate synthetic SystemVerilog projects for the u9 slang-limit evidence.

Two shapes:

* ``tree`` — the realistic project shape used for the module-catalog measurement:
  ``top`` -> 40 x ``mid_i`` -> 29 x ``leaf_{i}_{j}`` (1201 files, depth 3).
  ``--body-lines`` inflates every leaf/mid body, which is how a real 20-100x
  larger module is modelled.
* ``flat`` — one module per file; ``--warnings`` adds width-truncation
  assignments (one ``WidthTruncate`` diagnostic each) for the lint-volume
  measurement.

The generator is deterministic: the same arguments produce the same tree.

The output is generated on demand under ``scratch/`` and is not committed; the
generator itself is the reproducible fixture.
"""
import argparse
import pathlib


def padding(lines: int, index: int) -> str:
    """Deterministic filler declarations (kept combinatorial, warning-free)."""
    out = []
    for line in range(lines):
        out.append(f"  logic [7:0] pad_{line};")
        out.append(f"  assign pad_{line} = din ^ 8'd{(line + index) & 255};")
    return "\n".join(out)


def leaf(index: int, body_lines: int, warnings: int) -> str:
    extra = padding(body_lines, index)
    warning_lines = []
    for warning in range(warnings):
        warning_lines.append(f"  logic [3:0] narrow_{warning};")
        warning_lines.append(f"  assign narrow_{warning} = din;")
    return "\n".join(
        [
            f"// synthetic leaf {index}",
            f"module leaf_{index} (input logic clk, input logic rst_n,"
            " input logic [7:0] din, output logic [7:0] q);",
            "  logic [7:0] acc;",
            "  always_ff @(posedge clk or negedge rst_n)",
            "    if (!rst_n) acc <= 8'd0; else acc <= din;",
            "  assign q = acc;",
            extra,
            *warning_lines,
            "endmodule",
        ]
    ) + "\n"


def mid(index: int, leaves: int, body_lines: int, warnings: int) -> str:
    instances = "\n".join(
        f"  leaf_{index * leaves + leaf} u_leaf_{leaf} (.clk(clk), .rst_n(rst_n),"
        f" .din(din + 8'd{leaf}), .q());"
        for leaf in range(leaves)
    )
    terms = " ^ ".join(f"din[{leaf % 8}]" for leaf in range(leaves))
    extra = padding(body_lines, index)
    warning_lines = []
    for warning in range(warnings):
        warning_lines.append(f"  logic [3:0] narrow_{warning};")
        warning_lines.append(f"  assign narrow_{warning} = din;")
    return "\n".join(
        [
            f"// synthetic mid {index}",
            f"module mid_{index} (input logic clk, input logic rst_n,"
            " input logic [7:0] din, output logic [7:0] q);",
            "  logic [7:0] comb;",
            f"  assign comb = {terms};",
            "  assign q = comb;",
            instances,
            extra,
            *warning_lines,
            "endmodule",
        ]
    ) + "\n"


def top(mids: int) -> str:
    instances = "\n".join(
        f"  mid_{index} u_mid_{index} (.clk(clk), .rst_n(rst_n),"
        f" .din(din + 8'd{index}), .q());"
        for index in range(mids)
    )
    return "\n".join(
        [
            "// synthetic top",
            "module top (input logic clk, input logic rst_n,"
            " input logic [7:0] din, output logic [7:0] q);",
            instances,
            "  assign q = '0;",
            "endmodule",
        ]
    ) + "\n"


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--out", required=True)
    parser.add_argument("--shape", choices=("tree", "flat"), default="tree")
    parser.add_argument("--files", type=int, default=1176, help="flat shape only")
    parser.add_argument("--mids", type=int, default=40, help="tree shape only")
    parser.add_argument("--leaves-per-mid", type=int, default=29, help="tree shape only")
    parser.add_argument("--body-lines", type=int, default=0, help="padding declarations per file")
    parser.add_argument("--warnings", type=int, default=0, help="width-truncate assignments per file")
    args = parser.parse_args()

    root = pathlib.Path(args.out)
    root.mkdir(parents=True, exist_ok=True)
    if args.shape == "flat":
        for index in range(args.files):
            (root / f"mod_{index}.sv").write_text(
                leaf(index, args.body_lines, args.warnings), encoding="utf-8"
            )
        print(f"wrote {args.files} flat files to {root}")
        return

    (root / "top.sv").write_text(top(args.mids), encoding="utf-8")
    for mid_index in range(args.mids):
        (root / f"mid_{mid_index}.sv").write_text(
            mid(mid_index, args.leaves_per_mid, args.body_lines, args.warnings),
            encoding="utf-8",
        )
        for leaf_index in range(args.leaves_per_mid):
            index = mid_index * args.leaves_per_mid + leaf_index
            (root / f"leaf_{index}.sv").write_text(
                leaf(index, args.body_lines, args.warnings), encoding="utf-8"
            )
    total = 1 + args.mids + args.mids * args.leaves_per_mid
    print(f"wrote {total} tree files to {root}")


if __name__ == "__main__":
    main()
