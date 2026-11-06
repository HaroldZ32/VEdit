// synthetic module 33
module mod_33 (input logic clk, input logic [7:0] din, output logic [7:0] q);
  logic [7:0] acc;
  always_ff @(posedge clk) acc <= din;
  assign q = acc;
  logic [7:0] sig_0;
  assign sig_0 = din ^ 8'd0;
  logic [7:0] sig_1;
  assign sig_1 = din ^ 8'd1;
  mod_32 u_child (.clk(clk), .din(din ^ 8'd1), .q());
endmodule
