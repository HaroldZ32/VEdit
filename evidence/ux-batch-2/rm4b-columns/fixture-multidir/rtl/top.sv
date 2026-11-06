module top (input  logic       clk,
            output logic [3:0] y);
  child u_child (
      .clk (clk),
      .y   (y)
  );
endmodule
