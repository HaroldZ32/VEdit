// Offline-smoke fixture: a clean module that must lint with exit code 0.
module counter(input logic clk, input logic rst, output logic [7:0] q);
  always_ff @(posedge clk) begin
    if (rst) q <= '0;
    else q <= q + 8'd1;
  end
endmodule
