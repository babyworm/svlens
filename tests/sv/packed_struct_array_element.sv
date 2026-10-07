package packed_struct_array_pkg;
    typedef struct packed {
        logic [3:0] upper;
        logic [3:0] lower;
    } payload_t;
endpackage

module packed_struct_source(output packed_struct_array_pkg::payload_t o_data);
    assign o_data = '{upper: 4'hA, lower: 4'h5};
endmodule

module packed_struct_sink(input packed_struct_array_pkg::payload_t i_data);
endmodule

module packed_struct_whole_source(output packed_struct_array_pkg::payload_t [1:0] o_data);
    assign o_data[0] = '{upper: 4'h3, lower: 4'hC};
    assign o_data[1] = '{upper: 4'hC, lower: 4'h3};
endmodule

module packed_struct_whole_ascending_source(output packed_struct_array_pkg::payload_t [0:1] o_data);
    assign o_data[0] = '{upper: 4'h6, lower: 4'h9};
    assign o_data[1] = '{upper: 4'h9, lower: 4'h6};
endmodule

module packed_struct_array_top(input logic i_select);
    packed_struct_array_pkg::payload_t [1:0] bank;
    packed_struct_array_pkg::payload_t copied;
    packed_struct_array_pkg::payload_t copied_other;
    packed_struct_array_pkg::payload_t selected;
    packed_struct_array_pkg::payload_t [0:1] ascending_bank;
    packed_struct_array_pkg::payload_t ascending_copied;
    packed_struct_array_pkg::payload_t [1:0] whole_bank;
    packed_struct_array_pkg::payload_t whole_copied;
    packed_struct_array_pkg::payload_t [0:1] whole_ascending_bank;
    packed_struct_array_pkg::payload_t whole_ascending_copied;

    packed_struct_source u_source(.o_data(bank[0]));
    packed_struct_source u_other(.o_data(bank[1]));
    assign copied = bank[0];
    assign copied_other = bank[1];
    assign selected = bank[i_select];
    packed_struct_source u_ascending(.o_data(ascending_bank[0]));
    packed_struct_source u_ascending_other(.o_data(ascending_bank[1]));
    assign ascending_copied = ascending_bank[0];
    packed_struct_whole_source u_whole(.o_data(whole_bank));
    assign whole_copied = whole_bank[1];
    packed_struct_whole_ascending_source u_whole_ascending(.o_data(whole_ascending_bank));
    assign whole_ascending_copied = whole_ascending_bank[0];
    packed_struct_sink u_copied(.i_data(copied));
    packed_struct_sink u_other_copied(.i_data(copied_other));
    packed_struct_sink u_selected(.i_data(selected));
    packed_struct_sink u_ascending_copied(.i_data(ascending_copied));
    packed_struct_sink u_whole_copied(.i_data(whole_copied));
    packed_struct_sink u_whole_ascending_copied(.i_data(whole_ascending_copied));
endmodule
