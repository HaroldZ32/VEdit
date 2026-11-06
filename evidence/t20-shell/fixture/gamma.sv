module gamma (
  input  logic clk,
  output logic q
);
  always_ff @(posedge clk) begin
    q <= ~q;
  end
endmodule
