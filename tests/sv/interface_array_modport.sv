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

module indexed_bridge(indexed_bus.slave in_bus[1:0], indexed_bus.master out_bus[1:0]);
    for (genvar lane = 0; lane < 2; lane++) begin : gen_lane
        assign out_bus[lane].data = in_bus[lane].data;
        assign out_bus[lane].valid = in_bus[lane].valid;
    end
endmodule

module interface_array_forward_top;
    indexed_bus #(.Width(12)) in_buses[1:0]();
    indexed_bus #(.Width(12)) out_buses[1:0]();
    indexed_writer u_w0(.bus(in_buses[0]));
    indexed_writer u_w1(.bus(in_buses[1]));
    indexed_bridge u_bridge(.in_bus(in_buses), .out_bus(out_buses));
    indexed_reader u_r0(.bus(out_buses[0]));
    indexed_reader u_r1(.bus(out_buses[1]));
endmodule

module indexed_array_tap(indexed_bus.slave buses[0:1]);
endmodule

module interface_array_reversed_top;
    indexed_bus #(.Width(12)) buses[1:0]();
    indexed_writer u_w0(.bus(buses[0]));
    indexed_writer u_w1(.bus(buses[1]));
    indexed_array_tap u_tap(.buses(buses));
endmodule

module indexed_matrix_tap(indexed_bus.slave buses[1:0][1:0]);
endmodule

module interface_array_matrix_top;
    indexed_bus #(.Width(12)) buses[1:0][1:0]();
    indexed_writer u_w00(.bus(buses[0][0]));
    indexed_writer u_w01(.bus(buses[0][1]));
    indexed_writer u_w10(.bus(buses[1][0]));
    indexed_writer u_w11(.bus(buses[1][1]));
    indexed_matrix_tap u_tap(.buses(buses));
endmodule
