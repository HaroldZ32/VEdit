module delta (
  input  logic clk,
  output logic q
);
  always_ff @(posedge clk) begin
    q <= 1'b1;
  end
endmodule
