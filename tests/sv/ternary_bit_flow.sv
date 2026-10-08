module ternary_source(output logic [7:0] data_o);
    assign data_o = '0;
endmodule

module ternary_sink(input logic [7:0] data_i);
endmodule

module ternary_bit_flow(input logic select_i);
    logic [7:0] bus_a, bus_b, muxed;
    ternary_source u_a(.data_o(bus_a));
    ternary_source u_b(.data_o(bus_b));
    assign muxed = select_i ? bus_a : bus_b;
    ternary_sink u_sink(.data_i(muxed));
endmodule
