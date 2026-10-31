// ALU package: the shared type every RTL module imports.
package alu_pkg;

  typedef enum logic [1:0] {
    ALU_ADD = 2'b00,
    ALU_SUB = 2'b01,
    ALU_AND = 2'b10,
    ALU_OR  = 2'b11
  } alu_op_t;

  function automatic logic [7:0] reset_value();
    return 8'h00;
  endfunction

endpackage
