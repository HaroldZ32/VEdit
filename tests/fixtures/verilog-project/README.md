# Representative Verilog acceptance fixture (C13)

This is the repository-owned Verilog/SystemVerilog project every project-level
acceptance run opens. It exists so acceptance never depends on a real customer
project: the user's own tree stays an optional smoke test only.

Layout:

```text
project.f                      top-level filelist (the only .f at the project root,
                               so `*.f` discovery finds exactly one project)
filelists/tb.f                 nested filelist: testbench sources (+define+, +incdir+)
filelists/nested/tb_aux.f      second nesting level (project.f -> tb.f -> here)
filelists/extra.f              nested filelist with a duplicate entry
rtl/alu_pkg.sv                 package: shared enum + function
rtl/alu.sv                     module alu (imports alu_pkg, includes rtl/include header)
rtl/counter.v                  plain Verilog-2001 module (fixture mixes .v and .sv)
rtl/mem.sv                     synchronous memory, sized from `DEPTH
rtl/top.sv                     top level: instantiates alu, counter, mem
rtl/include/rtl_defs.vh        header found through +incdir+rtl/include
tb/tb_top.sv                   testbench: instantiates top
tb/tb_aux.sv                   helper module in the second-level filelist
tb/include/tb_defs.vh          header found through the nested filelist's +incdir+
inc/defs.vh                    project header (first +incdir+)
inc/generated/width.vh         generated header (+incdir+inc/generated)
expected/                      the resolved lists the acceptance script compares against
```

Filelist semantics this fixture pins down (see
`crates/vd-config/src/filelist.rs` for the implementation contract):

- `//` comments anywhere and `#` comments at line start or after whitespace;
- `+incdir+DIR` with several directories on one line, separated by `+`;
- `+define+NAME=VALUE+NAME2` (both the value and the bare form);
- nested `-f FILE` / `-F FILE`, resolved relative to **the containing
  filelist's directory** and recursive to a second level;
- a duplicate source entry inside one filelist (first occurrence wins, a
  warning is reported);
- `+libext+.v+.sv` and `-y rtl/lib`, tokens written for another simulator:
  reported as warnings and skipped, never fatal.

Every path inside a nested filelist is therefore written relative to that
filelist's own directory (`../rtl/...`). The `expected/` lists use
fixture-root-relative normalized paths so they stay readable; the script
computes the same normalization and fails on any difference.

Run the check from the repository root:

```sh
bash scripts/verify-fixture.sh            # report on stdout, exit 1 on any failure
bash scripts/verify-fixture.sh --evidence evidence/fixture
```

The fixture is also what the editor-side acceptance (AC-S1.6) opens: the
project root is this directory.
