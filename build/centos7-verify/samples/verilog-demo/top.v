// Sample top module: intentionally clean, so a lint run reports only bad.v.
module top (
    input  logic       clk,
    input  logic       rst_n,
    output logic [3:0] count
);
    counter #(.WIDTH(4)) u_counter (
        .clk   (clk),
        .rst_n (rst_n),
        .count (count)
    );
endmodule
