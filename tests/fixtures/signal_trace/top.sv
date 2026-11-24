module top(input logic clk, input wire a, b);
  wire first, second, merged, out1, out2;
  wire cycle_a, cycle_b;
  leaf u0(.clk(clk), .d(a), .q(first));
  leaf u1(.clk(clk), .d(b), .q(second));
  assign merged = first ^ second;
  assign out1 = merged;
  assign out2 = merged;
  assign cycle_a = cycle_b;
  assign cycle_b = cycle_a;
  wire bidi;
  io io0(.pad(bidi));
  assign bidi = a;
endmodule
