// Plain Verilog-2001 module: the fixture mixes .v and .sv sources.
`include "width.vh"

module counter #(
  parameter integer WIDTH_P = `WIDTH
) (
  input  wire               clk,
  input  wire               rst_n,
  output reg  [WIDTH_P-1:0] count
);

  always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      count <= {WIDTH_P{1'b0}};
    end else begin
      count <= count + 1'b1;
    end
  end

endmodule
