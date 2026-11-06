// T20 probe fixture: a real slang error (undeclared identifier) so the
// diagnostics panel has one genuine entry to jump from.
module beta (
  input  logic clk,
  output logic q
);
  always_ff @(posedge clk) begin
    q <= missing_signal;
  end
endmodule
