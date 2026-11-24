module leaf(input logic clk, input logic d, output wire q);
  logic state;
  always_ff @(posedge clk) state <= d;
  assign q = state;
endmodule

module io(inout wire pad);
  wire readback;
  assign readback = pad;
  assign pad = 1'bz;
endmodule
