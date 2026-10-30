module   messy_top(
input logic clk,
output logic q
);
always_ff@(posedge clk) begin
q<=1'b0;
end
endmodule
