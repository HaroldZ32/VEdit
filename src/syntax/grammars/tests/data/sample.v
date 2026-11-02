// Verilog sample: counter + FSM + generate
`timescale 1ns / 1ps

module counter #(
  parameter WIDTH = 8
) (
  input  wire clk,
  input  wire rst_n,
  output reg [WIDTH-1:0] q
);
  localparam MAX = (1 << WIDTH) - 1;
  wire enable;
  reg [1:0] state;

  assign enable = (state == 2'd0);

  always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      q <= {WIDTH{1'b0}};
      state <= 2'd0;
    end else if (enable) begin
      q <= q + 1'b1;
    end else begin
      q <= q;
    end
  end

  always @(*) begin
    case (state)
      2'd0: state = 2'd1;
      2'd1: state = 2'd2;
      default: state = 2'd0;
    endcase
  end

  function [WIDTH-1:0] mask(input [WIDTH-1:0] value);
    mask = value & MAX;
  endfunction

  task pulse;
    $display("pulse %0d", q);
    q <= q + 1;
  endtask

  generate
    genvar i;
    for (i = 0; i < WIDTH; i = i + 1) begin : gen_bit
      assign q[i] = q[i] ^ enable;
    end
  endgenerate

  sub_module u_sub (.a(q), .b(enable));
endmodule

module sub_module (input [7:0] a, input b);
  wire c;
  assign c = a[0] & b;
endmodule
