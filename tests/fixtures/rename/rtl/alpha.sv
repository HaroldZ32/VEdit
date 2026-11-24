`include "defs.vh"
module alpha #(
  parameter int WIDTH = `RENAME_WIDTH
) (
  input  logic             clk,
  input  logic [WIDTH-1:0] din,
  output logic [WIDTH-1:0] dout
);
  logic [WIDTH-1:0] alpha_cnt;
  localparam int LOCAL_DEPTH = cfg::DEPTH;
  logic [WIDTH-1:0] cnt;
  always_ff @(posedge clk) begin
    alpha_cnt <= din;
    cnt       <= alpha_cnt;
  end
  assign dout = cnt;
endmodule
