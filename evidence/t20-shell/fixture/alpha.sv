// T20 probe fixture: search hits (`always_ff`) + a semantic breadcrumb path
// (module alpha > function inc) at the cursor line.
module alpha (
  input  logic       clk,
  output logic [7:0] q
);
  function automatic logic [7:0] inc(input logic [7:0] v);
    inc = v + 8'd1;
  endfunction

  always_ff @(posedge clk) begin
    q <= inc(q);
  end
endmodule
