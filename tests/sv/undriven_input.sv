module block_with_config (
    input  logic [7:0] i_config,
    input  logic       i_enable,
    output logic [7:0] o_result
);
    assign o_result = i_enable ? i_config : 8'h0;
endmodule

module undriven_top;
    logic [7:0] result;
    logic enable;
    assign enable = 1'b1;
    block_with_config u_block (.i_config(), .i_enable(enable), .o_result(result));
endmodule
