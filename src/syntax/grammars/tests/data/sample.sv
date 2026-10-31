// SystemVerilog sample: package + interface + class + always_ff/comb
package alu_pkg;
  typedef enum logic [1:0] {ADD, SUB, MUL} op_t;
  localparam int WIDTH = 16;
endpackage

interface alu_if (input logic clk);
  logic [15:0] a;
  logic [15:0] b;
  logic [1:0]  op;
  logic [15:0] y;
endinterface

class alu_driver;
  rand logic [1:0] op;
  function new();
    op = ADD;
  endfunction
  function void set_op(input logic [1:0] v);
    op = v;
  endfunction
  task drive(input logic [15:0] a, input logic [15:0] b);
    $display("drive %0d %0d", a, b);
  endtask
endclass

module alu_top (
  input  logic clk,
  input  logic rst_n,
  output logic [15:0] y
);
  import alu_pkg::*;
  op_t op;
  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) op <= ADD;
    else        op <= SUB;
  end
  always_comb begin
    case (op)
      ADD: y = 16'h0001;
      SUB: y = 16'h0002;
      default: y = 16'h0000;
    endcase
  end
  generate
    for (genvar i = 0; i < 16; i++) begin : g_bits
      assign y[i] = y[i] ^ clk;
    end
  endgenerate
endmodule
