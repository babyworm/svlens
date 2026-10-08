module slice_source(output logic [7:0] o_data);
    assign o_data = 8'hA5;
endmodule

module slice_sink(input logic [3:0] i_data);
endmodule

module slice_bit_sink(input logic i_data);
endmodule

module slice_overlap_top;
    logic [7:0] shared, other;
    slice_source u_src(.o_data(shared));
    slice_sink u_low(.i_data(shared[3:0]));
    slice_sink u_high(.i_data(shared[7:4]));
    slice_bit_sink u_bit(.i_data(shared[5]));
    slice_sink u_other(.i_data(other[3:0]));
endmodule
