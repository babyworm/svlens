module wide_missing_sync (
    input logic clk_a, clk_b, rst_n,
    input logic [7:0] d_i,
    output logic [7:0] q_o
);
    logic [7:0] source_q;
    always_ff @(posedge clk_a or negedge rst_n) begin
        if (!rst_n) source_q <= '0;
        else source_q <= d_i;
    end
    always_ff @(posedge clk_b or negedge rst_n) begin
        if (!rst_n) q_o <= '0;
        else q_o <= source_q;
    end
endmodule
