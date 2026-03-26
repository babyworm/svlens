module core (
    output logic [31:0] o_data,
    output logic        o_valid,
    output logic [7:0]  o_debug,
    output logic signed [15:0] o_coeff
);
    assign o_data = 32'hDEAD;
    assign o_valid = 1'b1;
    assign o_debug = 8'hFF;
    assign o_coeff = -16'sd42;
endmodule

module bus_adapter (
    input  logic [15:0] i_data,
    input  logic        i_valid,
    input  logic [15:0] i_coeff,
    input  logic [7:0]  i_config,
    output logic [15:0] o_result
);
    assign o_result = i_valid ? i_data + i_coeff + {8'b0, i_config} : 16'b0;
endmodule

module integration_top;
    logic [31:0] data;
    logic        valid;
    logic [7:0]  debug;
    logic signed [15:0] coeff;
    logic [15:0] result;

    core u_core (
        .o_data(data),
        .o_valid(valid),
        .o_debug(),
        .o_coeff(coeff)
    );

    bus_adapter u_bus (
        .i_data(data[15:0]),
        .i_valid(valid),
        .i_coeff(coeff),
        .i_config(),
        .o_result(result)
    );
endmodule
