module top_b (input logic clk, output logic [3:0] y);
  shared u_shared (.a(clk), .y(y));
  logic spare;
  shared u_shared_2 (.a(clk), .y());
endmodule
