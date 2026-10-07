module cast_source(output logic [3:0] data_o);
    assign data_o = 4'hf;
endmodule

module cast_sink(input logic signed [31:0] data_i);
endmodule

module unsupported_width_cast;
    logic [3:0] data;
    logic signed [31:0] typed;
    cast_source u_source(.data_o(data));
    assign typed = int'(data[3:0]);
    cast_sink u_sink(.data_i(typed));
endmodule
