// UI-Mosaic F2 pixel fixture: one Verilog file that exercises every token class
// the KSyntaxHighlighting definitions colour differently (comments, strings,
// numbers, keywords, types, preprocessor, system tasks, functions, operators).
// Used by both the probe and the two real-window screenshot runs.
`timescale 1ns / 1ps
`define WIDTH 8
`define HEX_MASK 32'hDEAD_BEEF

module syntax_mosaic #(
    parameter int unsigned WIDTH = `WIDTH,
    parameter logic [WIDTH-1:0] RESET = {WIDTH{1'b0}}
) (
    input  logic                 clk_i,      // clock
    input  logic                 rst_ni,     // active-low reset
    input  logic [WIDTH-1:0]     data_i,
    output logic [WIDTH-1:0]     data_o,
    output logic                 valid_o
);

    // A line comment: keywords inside a comment are not keywords.
    /* A block comment with the same trap: module always_ff 42 3.14 "text" */

    localparam int unsigned HALF = WIDTH / 2;
    localparam real         PI   = 3.14159;
    localparam string       NAME = "VEdit \"syntax\" mosaic\n";

    typedef enum logic [1:0] {
        IDLE = 2'b00,
        RUN  = 2'b01,
        DONE = 2'b10
    } state_t;

    state_t state_q, state_d;
    logic [WIDTH-1:0] accum_q;
    wire  [HALF-1:0]  half_w;
    reg   [3:0]       nibble_r;
    int               count;

    assign half_w = data_i[HALF-1:0];
    assign data_o = accum_q ^ {WIDTH{valid_o}};

    always_ff @(posedge clk_i or negedge rst_ni) begin : p_state
        if (!rst_ni) begin
            state_q <= IDLE;
            accum_q <= '0;
            count   <= 0;
        end else begin
            state_q <= state_d;
            accum_q <= (accum_q << 1) + data_i;
            count   <= count + 1;
        end
    end

    always_comb begin : p_next
        state_d = state_q;
        valid_o = 1'b0;
        unique case (state_q)
            IDLE:    if (data_i != `HEX_MASK) state_d = RUN;
            RUN:     if (count >= HALF)       state_d = DONE;
            DONE:    begin
                state_d = IDLE;
                valid_o = 1'b1;
            end
            default: state_d = IDLE;
        endcase
    end

    function automatic logic [WIDTH-1:0] reverse(input logic [WIDTH-1:0] value);
        logic [WIDTH-1:0] result;
        for (int i = 0; i < WIDTH; i++) begin
            result[i] = value[WIDTH-1-i];
        end
        return result;
    endfunction

    task automatic report(input string message);
        $display("mosaic: %s count=%0d time=%0t", message, count, $time);
        $fflush();
    endtask

    generate
        for (genvar g = 0; g < 2; g++) begin : g_bank
            logic [3:0] bank_q; // 4'b1010 / 8'hFF / 16'd1024 / 1.5e3
            assign bank_q = nibble_r ^ 4'(g);
        end
    endgenerate

endmodule
