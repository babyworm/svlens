module svlens_axi_manager(AXI_LITE.Master bus);
  assign bus.aw_addr = 8'h10;
  assign bus.aw_prot = '0;
  assign bus.aw_valid = 1'b1;
  assign bus.w_data = 32'h1234_5678;
  assign bus.w_strb = 4'hf;
  assign bus.w_valid = 1'b1;
  assign bus.b_ready = 1'b1;
  assign bus.ar_addr = 8'h10;
  assign bus.ar_prot = '0;
  assign bus.ar_valid = 1'b1;
  assign bus.r_ready = 1'b1;
endmodule

module svlens_axi_subordinate(AXI_LITE.Slave bus);
  assign bus.aw_ready = 1'b1;
  assign bus.w_ready = 1'b1;
  assign bus.b_resp = axi_pkg::RESP_OKAY;
  assign bus.b_valid = 1'b1;
  assign bus.ar_ready = 1'b1;
  assign bus.r_data = 32'h5678_1234;
  assign bus.r_resp = axi_pkg::RESP_OKAY;
  assign bus.r_valid = 1'b1;
endmodule

module svlens_axi_xbar_probe(input logic clk_i, rst_ni);
  localparam axi_pkg::xbar_cfg_t Cfg = '{
    NoSlvPorts: 2, NoMstPorts: 2, MaxMstTrans: 2, MaxSlvTrans: 2,
    FallThrough: 1'b0, LatencyMode: axi_pkg::NO_LATENCY, PipelineStages: 0,
    AxiIdWidthSlvPorts: 1, AxiIdUsedSlvPorts: 1, UniqueIds: 1'b0,
    AxiAddrWidth: 8, AxiDataWidth: 32, NoAddrRules: 2
  };
  AXI_LITE #(.AXI_ADDR_WIDTH(8), .AXI_DATA_WIDTH(32)) slv_if [1:0] ();
  AXI_LITE #(.AXI_ADDR_WIDTH(8), .AXI_DATA_WIDTH(32)) mst_if [1:0] ();
  axi_pkg::xbar_rule_64_t [1:0] addr_map;
  assign addr_map[0] = '{idx: 0, start_addr: 0, end_addr: 128};
  assign addr_map[1] = '{idx: 1, start_addr: 128, end_addr: 256};

  svlens_axi_manager u_m0(.bus(slv_if[0]));
  svlens_axi_manager u_m1(.bus(slv_if[1]));
  svlens_axi_subordinate u_s0(.bus(mst_if[0]));
  svlens_axi_subordinate u_s1(.bus(mst_if[1]));

  axi_lite_xbar_intf #(.Cfg(Cfg)) u_xbar (
    .clk_i, .rst_ni, .slv_ports(slv_if), .mst_ports(mst_if),
    .addr_map_i(addr_map), .en_default_mst_port_i('0), .default_mst_port_i('0)
  );
endmodule
