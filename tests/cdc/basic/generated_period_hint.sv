module period_divider_cell(input logic clk_i, rst_n, output logic clk_o);
    always_ff @(posedge clk_i or negedge rst_n)
        if (!rst_n) clk_o <= 1'b0;
        else clk_o <= ~clk_o;
endmodule

module generated_period_hint(input logic clk_fast, rst_n, d_i, output logic q_o);
    logic clk_slow;
    period_divider_cell u_div(.clk_i(clk_fast), .rst_n, .clk_o(clk_slow));
    logic fast_q;
    always_ff @(posedge clk_fast or negedge rst_n)
        if (!rst_n) fast_q <= 1'b0;
        else fast_q <= d_i;
    always_ff @(posedge clk_slow or negedge rst_n)
        if (!rst_n) q_o <= 1'b0;
        else q_o <= fast_q;
endmodule
