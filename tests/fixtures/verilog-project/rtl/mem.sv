// Synchronous memory: depth and width come from the project.f defines, so this
// module needs the resolved project environment to compile.
module mem #(
  parameter integer DEPTH_P = `DEPTH,
  parameter integer WIDTH_P = `WIDTH
) (
  input  logic                       clk,
  input  logic                       we,
  input  logic [WIDTH_P-1:0]         wdata,
  input  logic [$clog2(DEPTH_P)-1:0] addr,
  output logic [WIDTH_P-1:0]         rdata
);

  logic [WIDTH_P-1:0] storage [0:DEPTH_P-1];

  always_ff @(posedge clk) begin
    if (we) begin
      storage[addr] <= wdata;
    end
    rdata <= storage[addr];
  end

endmodule
