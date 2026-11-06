// T37 fixture: the three path shapes the T46 hierarchy fix must hand to the
// module tree (T17's §C/D rerun):
//   top.u_mid.g_leaf[0].u   generate-for, labelled loop block  g_leaf
//   top.u_mid.g_if.u_if     generate-if,  labelled block       g_if
//   top.u_arr[3]            instance array element under top
module top (input logic clk, input logic [3:0] d, output logic [5:0] q);
  logic [5:0] mid_q;

  mid u_mid (.clk(clk), .d(d), .q(mid_q));
  leaf u_arr [3:0] (.clk(clk), .d(d[0]), .q());

  assign q = mid_q;
endmodule
