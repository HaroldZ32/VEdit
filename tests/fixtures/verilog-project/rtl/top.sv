// Top level: instantiates alu, counter and mem, so the fixture has real
// hierarchy for the project model and the LSP to walk.
`include "defs.vh"

module top #(
  parameter integer WIDTH_P = `WIDTH
) (
  input  logic                      clk,
  input  logic                      rst_n,
  input  logic                      we,
  input  logic [WIDTH_P-1:0]        a,
  input  logic [WIDTH_P-1:0]        b,
  input  alu_pkg::alu_op_t          op,
  input  logic [$clog2(`DEPTH)-1:0] addr,
  output logic [WIDTH_P-1:0]        y,
  output logic [WIDTH_P-1:0]        rdata,
  output logic [WIDTH_P-1:0]        count
);

  logic [WIDTH_P-1:0] alu_result;

  alu u_alu (
    .a  (a),
    .b  (b),
    .op (op),
    .y  (alu_result)
  );

  counter #(.WIDTH_P(WIDTH_P)) u_counter (
    .clk   (clk),
    .rst_n (rst_n),
    .count (count)
  );

  mem u_mem (
    .clk   (clk),
    .we    (we),
    .wdata (alu_result),
    .addr  (addr),
    .rdata (rdata)
  );

  assign y = alu_result;

`ifdef SIMULATION
  initial $display("[top] simulated build: addr width is %0d", $clog2(`DEPTH));
`endif

endmodule
