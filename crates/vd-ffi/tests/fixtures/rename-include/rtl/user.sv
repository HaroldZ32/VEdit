`include "defs.vh"
module user;
  logic [`CNT_W-1:0] local_bus;
  assign cfg::shared_cnt = local_bus;
endmodule
