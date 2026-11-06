module mid (input logic a, output logic [3:0] b);
  logic [1:0] t;
  genvar g;
  generate
    for (g = 0; g < 2; g++) begin : g_leaf
      leaf u (.a(a), .b(t[g]));
    end
  endgenerate
  if (1) begin : g_if
    leaf u_if (.a(a), .b(b[0]));
  end
  assign b[1] = t[0];
  assign b[2] = t[1];
  assign b[3] = 1'b0;
endmodule
