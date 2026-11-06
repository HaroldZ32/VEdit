module top;
  logic clk;
  logic [3:0] d, q;
  leaf u_leaf(.clk(clk), .d(d), .q(q));
endmodule
