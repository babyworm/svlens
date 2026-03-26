module source_mod (
    output logic [7:0] o_data,
    output logic       o_valid
);
    assign o_data = 8'hAB;
    assign o_valid = 1'b1;
endmodule

module sink_mod (
    input logic [7:0] i_data,
    input logic       i_valid
);
endmodule

module clean_top;
    logic [7:0] data;
    logic       valid;
    source_mod u_src (.o_data(data), .o_valid(valid));
    sink_mod   u_snk (.i_data(data), .i_valid(valid));
endmodule
