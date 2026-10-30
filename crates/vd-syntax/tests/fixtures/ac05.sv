// AC-05 fixture: one token per probed category, first occurrence is the token.
// COMMENT_TOKEN: comment category
`define MACRO_DEFINE_VALUE
module ac05_top #(
  parameter int W = 42
) (
  input  logic         clk,
  input  logic         rst_n,
  output logic [W-1:0] q
);
  always_ff @(posedge clk) begin
    if (!rst_n) q <= '0;
    else        q <= q + 1'b1;
  end
  initial $display("STRING_LITERAL = %0d", q);
endmodule

interface ac05_bus;
  logic valid;
  modport master (output valid);
endinterface

package ac05_pkg;
  typedef enum logic [1:0] { IDLE, RUN } state_t;
endpackage

class ac05_item;
  rand int unsigned len;
  constraint c_len { len inside {[1:64]}; }
endclass

covergroup cg_len with function sample(int len);
  coverpoint len { bins low = {[1:8]}; }
endgroup
