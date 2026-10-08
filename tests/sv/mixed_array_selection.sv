module mixed_array_source(output logic [1:0][3:0] o_data);
endmodule

module mixed_array_nibble_sink(input logic [3:0] i_data);
endmodule

module mixed_array_bit_sink(input logic i_bit);
endmodule

module mixed_array_nibble_source(output logic [3:0] o_data);
endmodule

module mixed_array_top(input logic bank_i, input logic [1:0] nibble_i);
    typedef struct packed {
        logic [1:0][3:0] data;
        logic [1:0][3:0] other;
    } payload_t;
    logic [1:0][3:0] bank [0:1];
    logic [1:0][3:0] bank_extra [0:1];
    logic [1:0][3:0] subbank [0:1];
    logic [1:0][3:0] multi_bank [0:1][0:1];
    payload_t struct_bank [0:1];
    logic [3:0] copied;
    mixed_array_source u_left(.o_data(bank[0]));
    mixed_array_source u_right(.o_data(bank[1]));
    mixed_array_source u_bank_extra(.o_data(bank_extra[0]));
    mixed_array_nibble_sink u_left_hi(.i_data(bank[0][1]));
    mixed_array_nibble_sink u_right_lo(.i_data(bank[1][0]));
    mixed_array_bit_sink u_left_bit(.i_bit(bank[0][1][2]));
    assign copied = bank[1][1];
    mixed_array_nibble_sink u_chain(.i_data(copied));
    mixed_array_bit_sink u_dynamic_outer(.i_bit(bank[bank_i][1][2]));
    mixed_array_nibble_sink u_dynamic_inner(.i_data(bank[0][nibble_i]));
    mixed_array_source u_multi(.o_data(multi_bank[1][0]));
    mixed_array_nibble_sink u_multi_hi(.i_data(multi_bank[1][0][1]));
    mixed_array_source u_struct(.o_data(struct_bank[0].data));
    mixed_array_nibble_sink u_struct_hi(.i_data(struct_bank[0].data[1]));
    mixed_array_nibble_sink u_struct_dynamic(.i_data(struct_bank[bank_i].data[1]));
    mixed_array_source u_other_field(.o_data(struct_bank[0].other));
    mixed_array_nibble_source u_other_lane(.o_data(subbank[0][0]));
    mixed_array_nibble_sink u_dynamic_fixed_lane(.i_data(subbank[bank_i][1]));
endmodule
