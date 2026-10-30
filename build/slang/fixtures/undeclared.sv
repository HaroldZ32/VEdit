// Offline-smoke fixture: `undefined_count` is used but never declared, so slang
// must report an error. Kept deliberately tiny: one known defect, nothing else.
module counter(input logic clk, input logic rst, output logic [7:0] q);
  always_ff @(posedge clk) begin
    if (rst) q <= '0;
    else q <= q + undefined_count;
  end
endmodule
