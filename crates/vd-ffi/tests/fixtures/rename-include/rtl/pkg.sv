`include "defs.vh"
package cfg;
`ifdef HAS_SHARED
  // The renamed signal. Its width comes from the include and its declaration
  // exists only when the include was found: without `-I inc` there is nothing
  // to rename, which is the negative control.
  logic [`CNT_W-1:0] shared_cnt;
`endif
endpackage
