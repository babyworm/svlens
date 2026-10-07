module inverted_reset_alias (
    input logic clk_a, clk_b, rst_n, d_i,
    output logic q_o
);
    logic reset_q, reset_hi;
    always_ff @(posedge clk_a or negedge rst_n) begin
        if (!rst_n) reset_q <= 1'b0;
        else reset_q <= 1'b1;
    end
    always_comb reset_hi = !reset_q;
    always_ff @(posedge clk_b or posedge reset_hi) begin
        if (reset_hi) q_o <= 1'b0;
        else q_o <= d_i;
    end
endmodule
