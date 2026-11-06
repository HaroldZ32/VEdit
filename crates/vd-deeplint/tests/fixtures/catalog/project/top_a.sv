module top_a (input logic clk, inout wire pad, output logic [3:0] y);
  bus_if bus();
  logic link;
  shared u_shared (.a(clk), .y(y));
  consumer u_consumer (.bus(bus), .clk(clk), .y());
  reflink u_ref (.x(link));
endmodule
