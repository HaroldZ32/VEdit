module top_b (input logic clk, output logic [3:0] y);
  shared u_shared (.a(clk), .y(y));
endmodule
