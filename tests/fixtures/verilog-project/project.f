// VEdit representative Verilog fixture: top-level filelist.
// Exercises every filelist feature the project model must support (C13):
//   * // comments and # comments, full line and trailing
//   * +incdir+ with several directories on one line
//   * +define+ with NAME=VALUE and bare NAME forms
//   * nested -f filelists, resolved relative to the containing filelist
//   * duplicate entries (first occurrence wins) and tokens for another simulator

+incdir+inc
+incdir+inc/generated+rtl/include

+define+WIDTH=8+DEPTH=16
+define+DEBUG

rtl/alu_pkg.sv        // package first: elaboration order
rtl/alu.sv
rtl/counter.v        # plain Verilog-2001 module
rtl/top.sv

// Not understood by VEdit: must be skipped with a warning, never fatal.
+libext+.v+.sv
-y rtl/lib

-f filelists/tb.f
-f filelists/extra.f
