module clock_as_data_alias_child (
    input logic sample_clk,
    input logic data_i,
    output logic q_o
);
    always_ff @(posedge sample_clk) q_o <= data_i;
endmodule

module clock_as_data_alias (
    input logic clk_a,
    input logic clk_b,
    output logic q_o
);
    clock_as_data_alias_child u_child (
        .sample_clk(clk_b),
        .data_i(clk_a),
        .q_o(q_o)
    );
endmodule
