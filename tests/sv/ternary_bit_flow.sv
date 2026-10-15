module ternary_source(output logic [7:0] data_o);
    assign data_o = '0;
endmodule

module ternary_sink(input logic [7:0] data_i);
endmodule

module ternary_select_source(output logic select_o);
    assign select_o = 1'b0;
endmodule

module ternary_bit_flow;
    logic [7:0] bus_a, bus_b, muxed;
    logic select_i;
    ternary_source u_a(.data_o(bus_a));
    ternary_source u_b(.data_o(bus_b));
    ternary_select_source u_select(.select_o(select_i));
    assign muxed = select_i ? bus_a : bus_b;
    ternary_sink u_sink(.data_i(muxed));
endmodule

module ternary_sink4(input logic [3:0] data_i);
endmodule

module ternary_concat_control_top;
    logic [7:0] bus_a, bus_b;
    logic [3:0] selected_high, fixed_low;
    logic select_i;
    ternary_source u_a(.data_o(bus_a));
    ternary_source u_b(.data_o(bus_b));
    ternary_select_source u_select(.select_o(select_i));
    assign {selected_high, fixed_low} = {select_i ? bus_a[7:4] : bus_b[7:4], bus_a[3:0]};
    ternary_sink4 u_high(.data_i(selected_high));
    ternary_sink4 u_low(.data_i(fixed_low));
endmodule
