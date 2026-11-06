module consumer (bus_if bus, input logic clk, output logic [3:0] y);
  assign y = {4{clk}};
endmodule
