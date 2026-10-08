module dynamic_bit_source(output logic o_data);
    assign o_data = 1'b1;
endmodule

module dynamic_bit_sink(input logic i_data);
endmodule

module dynamic_index_top(input logic [2:0] source_index, sink_index);
    logic [7:0] bus;
    logic source_bit;
    dynamic_bit_source u_src(.o_data(source_bit));
    always_comb begin
        bus = '0;
        bus[source_index] = source_bit;
    end
    dynamic_bit_sink u_sink(.i_data(bus[sink_index]));
endmodule
