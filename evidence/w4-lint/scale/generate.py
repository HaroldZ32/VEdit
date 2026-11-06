#!/usr/bin/env python3
"""A-12 deterministic scale fixture generator.

    python3 evidence/w4-lint/scale/generate.py --out target/tmp/scale-1k --files 1000
    python3 evidence/w4-lint/scale/generate.py --out target/tmp/scale-2k --files 2000

Shape (the user's real repository shape, A-9 features included):
  root/project.f            +incdir+$SCALE_ROOT/inc, +define+SCALE=1, -f lists/all.f
  root/lists/all.f          -f dirs/dN.f   (nested, relative to lists/)
  root/lists/dirs/dN.f      the 100 file entries for one directory (../../rtl/dN/...)
  root/rtl/dN/m_N_i.sv      the hierarchy root of that file

Two variants:
  --variant flat   (default, the A-12 baseline)
      leaf module + module with 10 PLAIN instances -> depth 2, 10 instances/file.
  --variant nested (ADDENDUM A-12 nested variant)
      leaf #(W) <- generate for(6) l4 #(W) <- l3 #(W) <- l2 #(W) <- l1 #(W) <- l0 #(W)
      chain depth 6 (l0.l1.l2.l3.l4.leaf), parameters propagated l0 -> leaf,
      one generate layer with 6 elaborated leaves: 10 instances/file.

Total instances = 10 * number of files (10k at 1000 files) in both variants. Every
file is generated from one template, so the output is deterministic.
"""
import argparse
import os
import shutil

FILES_PER_DIR = 100
INSTANCES_PER_FILE = 10
GENERATE_LEAVES = 6


def write(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(text)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--out", required=True)
    parser.add_argument("--files", type=int, default=1000)
    parser.add_argument("--variant", choices=("flat", "nested"), default="flat")
    args = parser.parse_args()

    if args.files % FILES_PER_DIR != 0:
        raise SystemExit("--files must be a multiple of %d" % FILES_PER_DIR)
    dirs = args.files // FILES_PER_DIR

    root = os.path.abspath(args.out)
    if os.path.isdir(root):
        shutil.rmtree(root)
    os.makedirs(root)

    write(os.path.join(root, "inc", "defs.vh"), "`define SCALE_WIDTH 8\n")
    write(os.path.join(root, "project.f"),
          "// A-12 scale fixture: $VAR + nested -f (three levels)\n"
          "+incdir+$SCALE_ROOT/inc\n"
          "+define+SCALE=1\n"
          "-f lists/all.f\n")

    all_f = []
    total_instances = 0
    for d in range(dirs):
        entries = []
        for i in range(FILES_PER_DIR):
            name = "m_%d_%d" % (d, i)
            path = os.path.join(root, "rtl", "d%d" % d, name + ".sv")
            if args.variant == "flat":
                # A-12 correction: each leaf drives its OWN net. Ten leaves
                # continuously assigning one `b` is 9 `multiple-cont-assigns`
                # errors, and slang then refuses to elaborate the design at all
                # (the A-12 lint numbers still measured, but elaboration could
                # never run on that fixture).
                instances = "\n".join(
                    "  leaf_%s u%d (.a(a), .b(b%d));" % (name, u, u)
                    for u in range(INSTANCES_PER_FILE)
                )
                nets = "\n".join("  logic b%d;" % u for u in range(INSTANCES_PER_FILE))
                write(path,
                      "module leaf_%s (input logic a, output logic b);\n"
                      "  assign b = a;\n"
                      "endmodule\n\n"
                      "module %s (input logic a);\n"
                      "%s\n"
                      "%s\n"
                      "endmodule\n" % (name, name, nets, instances))
            else:
                # Depth 6 chain with parameter propagation and one generate layer.
                # The generated leaves drive DISTINCT bits (b[g]) so the 6
                # elaborated instances do not fight over one variable.
                bus = "[%d:0]" % (GENERATE_LEAVES - 1)
                write(path,
                      "module leaf_%s #(parameter int W = 8)\n"
                      "  (input logic a, output logic b);\n"
                      "  localparam int LW = W;\n"
                      "  assign b = a;\n"
                      "endmodule\n\n"
                      "module l4_%s #(parameter int W = 7)\n"
                      "  (input logic a, output logic %s b);\n"
                      "  generate\n"
                      "    for (genvar g = 0; g < %d; g++) begin : g_leaf\n"
                      "      leaf_%s #(.W(W)) u (.a(a), .b(b[g]));\n"
                      "    end\n"
                      "  endgenerate\n"
                      "endmodule\n\n"
                      "module l3_%s #(parameter int W = 6)\n"
                      "  (input logic a, output logic %s b);\n"
                      "  l4_%s #(.W(W)) u4 (.a(a), .b(b));\n"
                      "endmodule\n\n"
                      "module l2_%s #(parameter int W = 5)\n"
                      "  (input logic a, output logic %s b);\n"
                      "  l3_%s #(.W(W)) u3 (.a(a), .b(b));\n"
                      "endmodule\n\n"
                      "module l1_%s #(parameter int W = 4)\n"
                      "  (input logic a, output logic %s b);\n"
                      "  l2_%s #(.W(W)) u2 (.a(a), .b(b));\n"
                      "endmodule\n\n"
                      "module l0_%s #(parameter int W = 3)\n"
                      "  (input logic a, output logic %s b);\n"
                      "  l1_%s #(.W(W)) u1 (.a(a), .b(b));\n"
                      "endmodule\n"
                      % (name, name, bus, GENERATE_LEAVES, name, name, bus, name, name, bus,
                         name, name, bus, name, name, bus, name))
            entries.append("../../rtl/d%d/%s.sv" % (d, name))
            total_instances += INSTANCES_PER_FILE
        write(os.path.join(root, "lists", "dirs", "d%d.f" % d), "\n".join(entries) + "\n")
        all_f.append("-f dirs/d%d.f" % d)
    write(os.path.join(root, "lists", "all.f"), "\n".join(all_f) + "\n")

    with open(os.path.join(root, "files.txt"), "w", encoding="utf-8") as handle:
        for d in range(dirs):
            for i in range(FILES_PER_DIR):
                handle.write("rtl/d%d/m_%d_%d.sv\n" % (d, i, i))

    print("GENERATED root=%s variant=%s dirs=%d files=%d instances=%d" %
          (root, args.variant, dirs, args.files, total_instances))


if __name__ == "__main__":
    main()
