interface bus_if;
  logic a;
  modport m(input a);
endinterface

module leaf(input logic a, output logic [3:0] y);
  assign y = {4{a}};
endmodule

module top(input logic clk, output logic [3:0] q);
  logic w;
  leaf u_leaf(.a(clk), .y(w));
  leaf u_leaf2(.a(clk), .y());
  assign q = w;
endmodule

module notused(input logic x, output logic z);
  assign z = x;
endmodule
