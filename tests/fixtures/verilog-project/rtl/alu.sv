// ALU: combinational; imports the package and the RTL header.
`include "rtl_defs.vh"

module alu
  import alu_pkg::*;
(
  input  logic [WIDTH-1:0] a,
  input  logic [WIDTH-1:0] b,
  input  alu_op_t          op,
  output logic [WIDTH-1:0] y
);

  always_comb begin
    unique case (op)
      ALU_ADD: y = a + b;
      ALU_SUB: y = a - b;
      ALU_AND: y = a & b;
      ALU_OR:  y = a | b;
      default: y = '0;
    endcase
  end

`ifdef DEBUG
  // DEBUG comes from project.f: this branch proves +define+ reaches the source.
  initial $display("[alu] DEBUG build: WIDTH=%0d", WIDTH);
`endif

endmodule
