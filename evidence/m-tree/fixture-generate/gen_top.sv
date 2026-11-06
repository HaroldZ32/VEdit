// T17 generate fixture: TWO roots (gen_top + other_top, other_top.sv) and a
// generate loop, so the module tree can be observed against a design that uses
// the construct every real design uses. Ground truth for this fixture:
//   roots: gen_top, other_top
//   instances: gen_top, gen_top.u_bank, gen_top.u_bank.g[0].u_leaf,
//              gen_top.u_bank.g[1].u_leaf, other_top          (= 5)
// The hierarchy service is known to walk only the first root and to skip
// GenerateBlockArray (cell task T46, owner se-i18n); this fixture records what
// the UI receives until that is fixed.
module leaf(input logic clk, input logic d, output logic q);
  assign q = d;
endmodule

module bank #(parameter int N = 2) (input logic clk, input logic [N-1:0] d, output logic [N-1:0] q);
  genvar i;
  generate
    for (i = 0; i < N; i++) begin : g
      leaf u_leaf (.clk(clk), .d(d[i]), .q(q[i]));
    end
  endgenerate
endmodule

module gen_top(input logic clk, input logic [1:0] d, output logic [1:0] q);
  bank u_bank (.clk(clk), .d(d), .q(q));
endmodule
