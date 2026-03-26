module leaf (
    input  logic [7:0] i_data,
    output logic [7:0] o_result
);
    assign o_result = i_data + 8'd1;
endmodule

module mid_block (
    input  logic [7:0] i_data,
    output logic [7:0] o_result,
    output logic [3:0] o_status
);
    leaf u_leaf (.i_data(i_data), .o_result(o_result));
    assign o_status = 4'hA;
endmodule

module deep_top;
    logic [7:0] data_in, data_out;
    logic [3:0] status;

    assign data_in = 8'h42;

    mid_block u_mid (
        .i_data(data_in),
        .o_result(data_out),
        .o_status(status)
    );
endmodule
