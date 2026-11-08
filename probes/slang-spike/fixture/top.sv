// Spike fixture (AC-3.0): top instantiates mid twice; the wire `link` is driven
// by u0.q (inside mid) and LOADED here by u1.d and the output q.
module top (
    input  logic clk,
    input  logic d,
    input  logic rst,
    output logic q
);
  logic link;

  mid u0 (.clk(clk), .d(d),       .q(link));
  mid u1 (.clk(clk), .d(link),    .q(q));

  // A second load of the same signal, in top's own continuous assignment.
  logic observed;
  assign observed = link;
endmodule
