// clean fixture
module top;
  logic clk;
  logic q;
  always_ff @(posedge clk) begin
    q <= 1'b0;
  end
endmodule
