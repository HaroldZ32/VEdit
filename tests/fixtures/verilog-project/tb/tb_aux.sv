// Helper module kept in a second-level filelist to exercise two-level nesting.
module tb_aux;

  logic [7:0] seen;

  initial begin
    seen = 8'h00;
  end

endmodule
