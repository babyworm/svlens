module unpacked_element_source(output logic [7:0] o_data);
    assign o_data = 8'hA5;
endmodule

module unpacked_element_sink(input logic [7:0] i_data);
endmodule

module unpacked_whole_source(output logic [7:0] o_data [0:1]);
    assign o_data[0] = 8'h3C;
    assign o_data[1] = 8'hC3;
endmodule

module unpacked_array_element_top(input logic i_select);
    logic [7:0] bank [0:1];
    logic [7:0] copied;
    logic [7:0] dynamic;
    logic [7:0] whole_bank [0:1];
    logic [7:0] whole_copied;

    unpacked_element_source u_source(.o_data(bank[0]));
    unpacked_element_source u_other(.o_data(bank[1]));
    assign copied = bank[0];
    assign dynamic = bank[i_select];
    unpacked_element_sink u_copied(.i_data(copied));
    unpacked_element_sink u_dynamic(.i_data(dynamic));
    unpacked_whole_source u_whole(.o_data(whole_bank));
    assign whole_copied = whole_bank[1];
    unpacked_element_sink u_whole_sink(.i_data(whole_copied));
endmodule
