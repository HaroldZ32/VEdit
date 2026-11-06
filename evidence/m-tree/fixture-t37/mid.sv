module mid (input logic clk, input logic [3:0] d, output logic [5:0] q);
  logic [5:0] t;
  genvar g;
  generate
    for (g = 0; g < 6; g++) begin : g_leaf
      leaf u (.clk(clk), .d(d[0]), .q(t[g]));
    end
  endgenerate
  if (1) begin : g_if
    leaf u_if (.clk(clk), .d(d[1]), .q(q[0]));
  end
  assign q[5:1] = t[5:1];
endmodule
