// COMMENT_TOKEN: VEdit gui-spike fixture
`define SPIKE_MACRO 8'd7

package pkg_spike;
  typedef enum logic [1:0] {IDLE, RUN, DONE} state_t;
endpackage

interface bus_if #(parameter int W = 8) (input logic clk);
  logic [W-1:0] data;
  modport master (output data, input clk);
endinterface

class spi_driver #(type T = int);
  rand int unsigned delay;
  constraint delay_c { delay inside {[1:5]}; }

  covergroup cg_delay;
    coverpoint delay { bins slow = {[4:5]}; }
  endgroup

  function new();
    cg_delay = new();
  endfunction
endclass

module spike_top (
  input  logic       clk,
  input  logic       rst_n,
  output logic [7:0] q
);

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      q <= 8'd42;  // NUMBER_TOKEN appears above
    end else begin
      $display("STRING_TOKEN value=%0d", q);
      q <= q + 1;
    end
  end

endmodule
