module nibble_source(output logic [3:0] o_data);
    assign o_data = 4'hA;
endmodule

module byte_sink(input logic [7:0] i_data);
endmodule

module nibble_sink(input logic [3:0] i_data);
endmodule

module slice_partial_drivers_top;
    logic [7:0] split_bus;
    nibble_source u_low(.o_data(split_bus[3:0]));
    nibble_source u_high(.o_data(split_bus[7:4]));
    byte_sink u_full(.i_data(split_bus));
    nibble_sink u_low_sink(.i_data(split_bus[3:0]));
endmodule
