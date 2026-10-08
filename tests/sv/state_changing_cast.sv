typedef bit [3:0] bits4_t;

module state_cast_source(output logic [3:0] data_o);
    assign data_o = 4'h9;
endmodule

module state_cast_sink(input bit [3:0] data_i);
endmodule

module state_changing_cast;
    logic [3:0] data;
    bits4_t two_state_data;
    state_cast_source u_source(.data_o(data));
    assign two_state_data = bits4_t'(data);
    state_cast_sink u_sink(.data_i(two_state_data));
endmodule
