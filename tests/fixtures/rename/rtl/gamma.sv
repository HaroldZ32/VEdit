module gamma (
  input  logic [7:0] din,
  output logic [7:0] dout
);
  logic [7:0] cnt;
  assign cnt  = din;
  assign dout = cnt;
endmodule
