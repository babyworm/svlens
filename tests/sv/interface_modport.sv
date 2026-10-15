interface simple_bus;
    logic [7:0] data;
    logic        valid;
    logic        ready;

    modport master (output data, output valid, input ready);
    modport slave  (input data, input valid, output ready);
endinterface

module producer (simple_bus.master bus);
    assign bus.data  = 8'hAB;
    assign bus.valid = 1'b1;
endmodule

module consumer (simple_bus.slave bus);
    assign bus.ready = 1'b1;
endmodule

module interface_modport (
    input  logic clk,
    input  logic rst_n
);
    simple_bus bus_inst();
    producer u_prod (.bus(bus_inst));
    consumer u_cons (.bus(bus_inst));
endmodule

module simple_bus_bridge(simple_bus.slave in_bus, simple_bus.master out_bus);
    assign out_bus.data = in_bus.data;
    assign out_bus.valid = in_bus.valid;
    assign in_bus.ready = out_bus.ready;
endmodule

module interface_modport_bridge;
    simple_bus in_bus();
    simple_bus out_bus();
    producer u_prod(.bus(in_bus));
    simple_bus_bridge u_bridge(.in_bus(in_bus), .out_bus(out_bus));
    consumer u_cons(.bus(out_bus));
endmodule
