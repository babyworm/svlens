module missing_sync_inline(input logic clk_a, clk_b, rst_n, data_in);
    logic q_a;
    // svlens: waive Ac_cdc01 path=missing_sync_inline.q_b reason: reviewed intentional crossing
    logic q_b;
    // svlens: waive Ac_cdc01 path=* reason: wildcard must not hide a crossing
    logic q_c;
    always_ff @(posedge clk_a or negedge rst_n)
        if (!rst_n) q_a <= 1'b0;
        else q_a <= data_in;
    always_ff @(posedge clk_b or negedge rst_n)
        if (!rst_n) q_b <= 1'b0;
        else q_b <= q_a;
    always_ff @(posedge clk_b or negedge rst_n)
        if (!rst_n) q_c <= 1'b0;
        else q_c <= q_a;
endmodule
