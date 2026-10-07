module nested_packed_source(output logic [1:0][2:0][7:0] o_data);
endmodule

module nested_packed_sink(input logic [7:0] i_data);
endmodule

module nested_packed_bit_sink(input logic i_bit);
endmodule

module nested_packed_array_top(input logic [1:0] index_i);
    logic [1:0][2:0][7:0] lanes;
    logic [0:1][0:2][7:0] ascending_lanes;
    logic [7:0] selected;
    nested_packed_source u_source(.o_data(lanes));
    nested_packed_sink u_high(.i_data(lanes[1][2]));
    nested_packed_sink u_low(.i_data(lanes[0][1]));
    nested_packed_bit_sink u_bit(.i_bit(lanes[1][2][3]));
    nested_packed_sink u_dynamic(.i_data(lanes[1][index_i]));
    assign selected = lanes[1][2];
    nested_packed_sink u_chain(.i_data(selected));
    nested_packed_source u_ascending_source(.o_data(ascending_lanes));
    nested_packed_sink u_ascending_high(.i_data(ascending_lanes[0][0]));
endmodule
