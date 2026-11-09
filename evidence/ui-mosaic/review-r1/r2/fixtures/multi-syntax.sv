// UI-Mosaic A8 fixture: one file, many syntax classes (keywords, types,
// comments, strings, numbers, preprocessor, attributes, operators).
`timescale 1ns/1ps
`define WIDTH 8
`define SIM

package mosaic_pkg;
  typedef enum logic [1:0] {IDLE = 2'b00, RUN = 2'b01, DONE = 2'b10} state_e;
  typedef struct packed {
    logic [7:0] data;
    logic       valid;
  } beat_t;
endpackage : mosaic_pkg

module mosaic_scheduler #(parameter int DEPTH = 16) (
  input  logic              clk_i,
  input  logic              rst_ni,
  input  mosaic_pkg::state_e state_i,
  output logic [7:0]        data_o,
  output logic              valid_o
);
  import mosaic_pkg::*;
  localparam int CNT_MAX = DEPTH - 1;
  beat_t beat_q;
  logic [31:0] counter_q;
  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      counter_q <= '0;
      beat_q    <= '{data: 8'h00, valid: 1'b0};
    end else begin
      case (state_i)
        IDLE: counter_q <= counter_q + 32'd1;
        RUN:  counter_q <= (counter_q == 32'(CNT_MAX)) ? '0 : counter_q + 32'd1;
        default: counter_q <= '0;
      endcase
      beat_q.data  <= counter_q[7:0];
      beat_q.valid <= (state_i == RUN);
    end
  end
  /* multi-line comment block: the quick brown fox jumps over the lazy dog */
  assign data_o  = beat_q.data;
  assign valid_o = beat_q.valid;
  initial $display("mosaic: %0d beats, width=%0d", DEPTH, `WIDTH);
endmodule : mosaic_scheduler
