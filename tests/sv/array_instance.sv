module channel (
    input  logic [7:0] i_data,
    output logic [7:0] o_data,
    output logic       o_valid
);
    assign o_data = i_data;
    assign o_valid = 1'b1;
endmodule

module collector (
    input logic [7:0] i_ch0_data,
    input logic [7:0] i_ch1_data,
    input logic       i_ch0_valid,
    input logic       i_ch1_valid
);
endmodule

module array_top;
    logic [7:0] src_data [2];
    logic [7:0] ch_data [2];
    logic       ch_valid [2];

    assign src_data[0] = 8'hAA;
    assign src_data[1] = 8'hBB;

    channel u_ch0 (.i_data(src_data[0]), .o_data(ch_data[0]), .o_valid(ch_valid[0]));
    channel u_ch1 (.i_data(src_data[1]), .o_data(ch_data[1]), .o_valid(ch_valid[1]));

    collector u_coll (
        .i_ch0_data(ch_data[0]),
        .i_ch1_data(ch_data[1]),
        .i_ch0_valid(ch_valid[0]),
        .i_ch1_valid(ch_valid[1])
    );
endmodule
