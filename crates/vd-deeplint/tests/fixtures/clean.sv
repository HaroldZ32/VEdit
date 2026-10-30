module clean_probe(input logic clk, output logic q);
  always_ff @(posedge clk) q <= ~q;
endmodule
