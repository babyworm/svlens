module svlens_axi_manager(
  input logic [7:0] aw_addr_i, ar_addr_i,
  input logic [2:0] aw_prot_i, ar_prot_i,
  input logic [31:0] w_data_i,
  input logic [3:0] w_strb_i,
  input logic aw_valid_i, w_valid_i, ar_valid_i, b_ready_i, r_ready_i,
  AXI_LITE.Master bus
);
  assign bus.aw_addr = aw_addr_i;
  assign bus.aw_prot = aw_prot_i;
  assign bus.aw_valid = aw_valid_i;
  assign bus.w_data = w_data_i;
  assign bus.w_strb = w_strb_i;
  assign bus.w_valid = w_valid_i;
  assign bus.b_ready = b_ready_i;
  assign bus.ar_addr = ar_addr_i;
  assign bus.ar_prot = ar_prot_i;
  assign bus.ar_valid = ar_valid_i;
  assign bus.r_ready = r_ready_i;
endmodule

module svlens_axi_subordinate(
  input logic aw_ready_i, w_ready_i, b_valid_i, ar_ready_i, r_valid_i,
  input logic [1:0] b_resp_i, r_resp_i,
  input logic [31:0] r_data_i,
  AXI_LITE.Slave bus
);
  assign bus.aw_ready = aw_ready_i;
  assign bus.w_ready = w_ready_i;
  assign bus.b_resp = b_resp_i;
  assign bus.b_valid = b_valid_i;
  assign bus.ar_ready = ar_ready_i;
  assign bus.r_data = r_data_i;
  assign bus.r_resp = r_resp_i;
  assign bus.r_valid = r_valid_i;
endmodule

module svlens_axi_xbar_probe(
  input logic clk_i, rst_ni,
  input logic [1:0][7:0] aw_addr_i, ar_addr_i,
  input logic [1:0][2:0] aw_prot_i, ar_prot_i,
  input logic [1:0][31:0] w_data_i, r_data_i,
  input logic [1:0][3:0] w_strb_i,
  input logic [1:0][1:0] b_resp_i, r_resp_i,
  input logic [1:0] aw_valid_i, w_valid_i, ar_valid_i, b_ready_i, r_ready_i,
  input logic [1:0] aw_ready_i, w_ready_i, b_valid_i, ar_ready_i, r_valid_i
);
  typedef struct packed {
    int unsigned idx;
    logic [7:0] start_addr;
    logic [7:0] end_addr;
  } rule_t;
  localparam axi_pkg::xbar_cfg_t Cfg = '{
    NoSlvPorts: 2, NoMstPorts: 2, MaxMstTrans: 2, MaxSlvTrans: 2,
    FallThrough: 1'b0, LatencyMode: axi_pkg::NO_LATENCY, PipelineStages: 0,
    AxiIdWidthSlvPorts: 1, AxiIdUsedSlvPorts: 1, UniqueIds: 1'b0,
    AxiAddrWidth: 8, AxiDataWidth: 32, NoAddrRules: 2
  };
  AXI_LITE #(.AXI_ADDR_WIDTH(8), .AXI_DATA_WIDTH(32)) slv_if [1:0] ();
  AXI_LITE #(.AXI_ADDR_WIDTH(8), .AXI_DATA_WIDTH(32)) mst_if [1:0] ();
  rule_t [1:0] addr_map;
  assign addr_map[0] = '{idx: 0, start_addr: 0, end_addr: 128};
  // An all-zero end denotes the upper end of the 8-bit address space.
  assign addr_map[1] = '{idx: 1, start_addr: 128, end_addr: 0};

  svlens_axi_manager u_m0(
    .aw_addr_i(aw_addr_i[0]), .ar_addr_i(ar_addr_i[0]),
    .aw_prot_i(aw_prot_i[0]), .ar_prot_i(ar_prot_i[0]),
    .w_data_i(w_data_i[0]), .w_strb_i(w_strb_i[0]),
    .aw_valid_i(aw_valid_i[0]), .w_valid_i(w_valid_i[0]), .ar_valid_i(ar_valid_i[0]),
    .b_ready_i(b_ready_i[0]), .r_ready_i(r_ready_i[0]), .bus(slv_if[0])
  );
  svlens_axi_manager u_m1(
    .aw_addr_i(aw_addr_i[1]), .ar_addr_i(ar_addr_i[1]),
    .aw_prot_i(aw_prot_i[1]), .ar_prot_i(ar_prot_i[1]),
    .w_data_i(w_data_i[1]), .w_strb_i(w_strb_i[1]),
    .aw_valid_i(aw_valid_i[1]), .w_valid_i(w_valid_i[1]), .ar_valid_i(ar_valid_i[1]),
    .b_ready_i(b_ready_i[1]), .r_ready_i(r_ready_i[1]), .bus(slv_if[1])
  );
  svlens_axi_subordinate u_s0(
    .aw_ready_i(aw_ready_i[0]), .w_ready_i(w_ready_i[0]), .b_valid_i(b_valid_i[0]),
    .ar_ready_i(ar_ready_i[0]), .r_valid_i(r_valid_i[0]),
    .b_resp_i(b_resp_i[0]), .r_resp_i(r_resp_i[0]), .r_data_i(r_data_i[0]),
    .bus(mst_if[0])
  );
  svlens_axi_subordinate u_s1(
    .aw_ready_i(aw_ready_i[1]), .w_ready_i(w_ready_i[1]), .b_valid_i(b_valid_i[1]),
    .ar_ready_i(ar_ready_i[1]), .r_valid_i(r_valid_i[1]),
    .b_resp_i(b_resp_i[1]), .r_resp_i(r_resp_i[1]), .r_data_i(r_data_i[1]),
    .bus(mst_if[1])
  );

  axi_lite_xbar_intf #(.Cfg(Cfg), .rule_t(rule_t)) u_xbar (
    .clk_i, .rst_ni, .slv_ports(slv_if), .mst_ports(mst_if),
    .addr_map_i(addr_map), .en_default_mst_port_i('0), .default_mst_port_i('0)
  );
endmodule
