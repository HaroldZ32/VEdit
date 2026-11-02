// Intentionally violation-laden file: the bundled verible lint must report > 0 diagnostics.
// 1) explicit-parameter-storage-type: `parameter` without a type
// 2) always-comb: a combinational block written as `always @(*)`
// 3) an over-long line (line-length) and an unused signal
module bad (
    input  wire       a,
    input  wire       b,
    output reg  [3:0] q
);
    parameter DEPTH = 8;
    wire unused_signal_that_nobody_reads_at_all_and_makes_the_line_longer_and_longer_than_one_hundred_characters_for_sure;
    always @(*) begin
        q = a + b;
    end
endmodule
