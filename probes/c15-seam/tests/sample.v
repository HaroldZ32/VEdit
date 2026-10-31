// Minimal Verilog sample for the C15 seam probe.
module counter (input clk, input rst, output reg q);
  wire n1;
  reg  state;
  assign n1 = clk & rst;
  always @(posedge clk) begin
    assign q = n1 | state;
    assign state = rst;
  end
endmodule

module top (input a, output b);
  wire t;
  assign t = a + 1;
  assign b = t;
endmodule
