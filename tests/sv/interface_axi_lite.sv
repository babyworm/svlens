package axi_lite_types;
    typedef logic [2:0] prot_t;
    typedef enum logic [1:0] {Okay, SlaveError, DecodeError} resp_t;
endpackage

interface axi_lite_if #(parameter int AddrWidth = 20, DataWidth = 32);
    logic [AddrWidth-1:0] awaddr;
    axi_lite_types::prot_t awprot;
    logic awvalid, awready;
    logic [DataWidth-1:0] wdata;
    logic [DataWidth/8-1:0] wstrb;
    logic wvalid, wready;
    axi_lite_types::resp_t bresp;
    logic bvalid, bready;
    logic [AddrWidth-1:0] araddr;
    axi_lite_types::prot_t arprot;
    logic arvalid, arready;
    logic [DataWidth-1:0] rdata;
    axi_lite_types::resp_t rresp;
    logic rvalid, rready;

    modport manager (output awaddr, awprot, awvalid, wdata, wstrb, wvalid, bready,
                           araddr, arprot, arvalid, rready,
                     input awready, wready, bresp, bvalid, arready, rdata, rresp, rvalid);
    modport subordinate (input awaddr, awprot, awvalid, wdata, wstrb, wvalid, bready,
                               araddr, arprot, arvalid, rready,
                         output awready, wready, bresp, bvalid, arready, rdata, rresp, rvalid);
endinterface

module axi_lite_manager(axi_lite_if.manager bus);
    assign bus.awaddr = '0;
    assign bus.awprot = '0;
    assign bus.awvalid = 1'b1;
    assign bus.wdata = '0;
    assign bus.wstrb = '1;
    assign bus.wvalid = 1'b1;
    assign bus.bready = 1'b1;
    assign bus.araddr = '0;
    assign bus.arprot = '0;
    assign bus.arvalid = 1'b1;
    assign bus.rready = 1'b1;
endmodule

module axi_lite_subordinate(axi_lite_if.subordinate bus);
    assign bus.awready = 1'b1;
    assign bus.wready = 1'b1;
    assign bus.bresp = axi_lite_types::Okay;
    assign bus.bvalid = 1'b1;
    assign bus.arready = 1'b1;
    assign bus.rdata = '0;
    assign bus.rresp = axi_lite_types::Okay;
    assign bus.rvalid = 1'b1;
endmodule

module interface_axi_lite;
    axi_lite_if #(.AddrWidth(20), .DataWidth(32)) bus_a();
    axi_lite_if #(.AddrWidth(20), .DataWidth(32)) bus_b();
    axi_lite_manager u_mgr_a(.bus(bus_a));
    axi_lite_subordinate u_sub_a(.bus(bus_a));
    axi_lite_manager u_mgr_b(.bus(bus_b));
    axi_lite_subordinate u_sub_b(.bus(bus_b));
endmodule
