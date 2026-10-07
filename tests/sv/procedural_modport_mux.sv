interface mux_bus;
    logic [7:0] data;
    modport master(output data);
endinterface

module mux_bus_producer(mux_bus.master bus);
    assign bus.data = 8'h3c;
endmodule

module mux_bus_sink(input logic [7:0] i_data);
endmodule

module procedural_modport_mux_top;
    mux_bus bus_a();
    mux_bus bus_b();
    logic select;
    logic [7:0] selected;

    mux_bus_producer u_a(.bus(bus_a));
    mux_bus_producer u_b(.bus(bus_b));

    always_comb selected = select ? bus_a.data : bus_b.data;

    mux_bus_sink u_out(.i_data(selected));
    mux_bus_sink u_tap(.i_data(bus_a.data));
endmodule
