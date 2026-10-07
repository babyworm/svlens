interface axi_lite_if #(parameter int AddrWidth = 20, DataWidth = 32);
    logic [AddrWidth-1:0] awaddr;
    logic awvalid, awready;
    logic [DataWidth-1:0] wdata;
    logic wvalid, wready;
    logic [1:0] bresp;
    logic bvalid, bready;

    modport manager (output awaddr, awvalid, wdata, wvalid, bready,
                     input awready, wready, bresp, bvalid);
    modport subordinate (input awaddr, awvalid, wdata, wvalid, bready,
                         output awready, wready, bresp, bvalid);
endinterface

module axi_lite_manager(axi_lite_if.manager bus);
    assign bus.awaddr = '0;
    assign bus.awvalid = 1'b1;
    assign bus.wdata = '0;
    assign bus.wvalid = 1'b1;
    assign bus.bready = 1'b1;
endmodule

module axi_lite_subordinate(axi_lite_if.subordinate bus);
    assign bus.awready = 1'b1;
    assign bus.wready = 1'b1;
    assign bus.bresp = 2'b00;
    assign bus.bvalid = 1'b1;
endmodule

module interface_axi_lite;
    axi_lite_if #(.AddrWidth(20), .DataWidth(32)) bus_a();
    axi_lite_if #(.AddrWidth(20), .DataWidth(32)) bus_b();
    axi_lite_manager u_mgr_a(.bus(bus_a));
    axi_lite_subordinate u_sub_a(.bus(bus_a));
    axi_lite_manager u_mgr_b(.bus(bus_b));
    axi_lite_subordinate u_sub_b(.bus(bus_b));
endmodule
