// Testbench: instantiates `top`, proving the hierarchy from the outside.
`include "tb_defs.vh"

module tb_top;

  localparam integer WIDTH_P = `WIDTH;
  localparam integer DEPTH_P = `DEPTH;

  logic                       clk = 1'b0;
  logic                       rst_n = 1'b0;
  logic                       we = 1'b0;
  logic [WIDTH_P-1:0]         a = '0;
  logic [WIDTH_P-1:0]         b = '0;
  alu_pkg::alu_op_t           op = alu_pkg::ALU_ADD;
  logic [$clog2(DEPTH_P)-1:0] addr = '0;
  logic [WIDTH_P-1:0]         y;
  logic [WIDTH_P-1:0]         rdata;
  logic [WIDTH_P-1:0]         count;

  always #5 clk = ~clk;

  top #(.WIDTH_P(WIDTH_P)) u_top (
    .clk   (clk),
    .rst_n (rst_n),
    .we    (we),
    .a     (a),
    .b     (b),
    .op    (op),
    .addr  (addr),
    .y     (y),
    .rdata (rdata),
    .count (count)
  );

  initial begin
    rst_n = 1'b1;
    a     = 8'd3;
    b     = 8'd4;
    #20;
    $display("tb_top: y=%0d", y);
    $finish;
  end

endmodule
