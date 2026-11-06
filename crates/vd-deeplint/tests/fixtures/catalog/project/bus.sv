interface bus_if;
  logic req;
  logic ack;
  modport master (output req, input ack);
endinterface
