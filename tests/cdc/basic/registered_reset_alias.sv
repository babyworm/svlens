module registered_reset_source(input logic clk_i, rst_n, output logic rst_sync_n);
    always_ff @(posedge clk_i or negedge rst_n)
        if (!rst_n) rst_sync_n <= 1'b0;
        else rst_sync_n <= 1'b1;
endmodule

module registered_reset_sink(input logic clk_i, rst_sync_n, d_i, output logic q_o);
    always_ff @(posedge clk_i or negedge rst_sync_n)
        if (!rst_sync_n) q_o <= 1'b0;
        else q_o <= d_i;
endmodule

module registered_reset_alias(input logic clk_a, clk_b, rst_n, d_i, output logic q_o);
    logic generated_reset, routed_reset;
    registered_reset_source u_source(.clk_i(clk_a), .rst_n(rst_n), .rst_sync_n(generated_reset));
    assign routed_reset = generated_reset;
    registered_reset_sink u_sink(.clk_i(clk_b), .rst_sync_n(routed_reset), .d_i(d_i), .q_o(q_o));
endmodule
