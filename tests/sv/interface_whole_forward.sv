interface route_bus;
    logic [7:0] data;
    logic valid;
endinterface

module route_writer(route_bus bus, input logic [7:0] data_i);
    assign bus.data = data_i;
    assign bus.valid = 1'b1;
endmodule

module route_leaf(route_bus bus, output logic [7:0] data_o);
    assign data_o = bus.data;
endmodule

module route_wrapper(route_bus bus, output logic [7:0] data_o);
    route_leaf u_leaf(.bus(bus), .data_o(data_o));
endmodule

module interface_whole_forward;
    route_bus shared();
    route_bus other();
    logic [7:0] data_i, observed, observed_other;
    route_writer u_prod(.bus(shared), .data_i(data_i));
    route_wrapper u_wrap(.bus(shared), .data_o(observed));
    route_writer u_other_prod(.bus(other), .data_i(data_i));
    route_wrapper u_other_wrap(.bus(other), .data_o(observed_other));
endmodule
