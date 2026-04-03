module producer (
    output logic o_data,
    output logic o_nc
);
    assign o_data = 1'b1;
    assign o_nc = 1'b0;
endmodule

module consumer (
    input logic i_data,
    input logic i_nc,
    input logic i_tie
);
endmodule

module filter_top;
    logic data;

    producer u_src (
        .o_data(data),
        .o_nc()
    );

    consumer u_snk (
        .i_data(data),
        .i_nc(),
        .i_tie(1'b0)
    );
endmodule
