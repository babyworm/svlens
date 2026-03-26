module producer (
    output logic [31:0] o_data,
    output logic signed [15:0] o_coeff,
    output logic [7:0] o_debug,
    output logic o_valid
);
    assign o_data = 32'hDEAD;
    assign o_coeff = -16'sd1;
    assign o_debug = 8'hFF;
    assign o_valid = 1'b1;
endmodule

module consumer (
    input logic [15:0] i_data,
    input logic [15:0] i_coeff,
    input logic [7:0] i_config,
    input logic i_valid
);
endmodule

module mixed_top;
    logic [31:0] data;
    logic signed [15:0] coeff;
    logic valid;
    logic [15:0] narrow_data;

    producer u_prod (
        .o_data(data),
        .o_valid(valid),
        .o_debug(),
        .o_coeff(coeff)
    );

    assign narrow_data = data[15:0];

    consumer u_cons (
        .i_data(narrow_data),
        .i_coeff(coeff),
        .i_config(),
        .i_valid(valid)
    );
endmodule
