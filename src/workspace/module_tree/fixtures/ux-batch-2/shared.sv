module shared (input logic a, output logic [3:0] y);
  assign y = {4{a}};
endmodule
