interface indexed_bus #(parameter int Width = 8);
    logic [Width-1:0] data;
    logic valid;
    modport master (output data, valid);
    modport slave (input data, valid);
endinterface

module indexed_writer(indexed_bus.master bus);
    assign bus.data = '0;
    assign bus.valid = 1'b1;
endmodule

module indexed_reader(indexed_bus.slave bus);
endmodule

module interface_array_modport;
    indexed_bus #(.Width(12)) buses[2]();
    indexed_writer u_w0(.bus(buses[0]));
    indexed_reader u_r0(.bus(buses[0]));
    indexed_writer u_w1(.bus(buses[1]));
    indexed_reader u_r1(.bus(buses[1]));
endmodule
