module beta (
  input  logic       clk,
  input  logic [7:0] din,
  output logic [7:0] dout
);
  // cnt is only a word in this comment
  logic [7:0] tmp;
  localparam int BETA_DEPTH = cfg::DEPTH;
  alpha #(.WIDTH(8)) u_alpha (.clk(clk), .din(din), .dout(tmp));
  assign dout = tmp;
endmodule
