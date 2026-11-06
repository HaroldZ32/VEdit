// The probe/function name above was the pre-rename copy of bank — that module
// lives in gen_top.sv; this file is the second root that "only the first root
// is walked" (hierarchy.rs) silently drops.
module other_top(input logic clk, output logic q);
  assign q = clk;
endmodule
