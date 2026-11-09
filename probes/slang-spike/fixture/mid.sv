// Spike fixture (AC-3.0): the signal `state` is DRIVEN here, inside mid.
module mid (
    input  logic clk,
    input  logic d,
    output logic q
);
  logic state;

  always_ff @(posedge clk) begin
    state <= d;
  end

  assign q = state;
endmodule
