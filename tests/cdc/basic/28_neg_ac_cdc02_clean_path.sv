// Test 28: Negative pair for Ac_cdc02 (combinational logic before sync FF).
// Mirror of fixture 05_comb_before_sync but with the sync chain driven
// directly from the source flop (no comb stage). Must NOT raise Ac_cdc02.
//
// Expected: 0 violations, 0 cautions, 1 INFO, 1 crossing.

module neg_ac_cdc02_clean_path (
    input  logic clk_a,
    input  logic clk_b,
    input  logic rst_n,
    input  logic data_in
);

    logic q_a;
    logic sync_ff1, sync_ff2;

    always_ff @(posedge clk_a or negedge rst_n) begin
        if (!rst_n)
            q_a <= 1'b0;
        else
            q_a <= data_in;
    end

    // No combinational stage between q_a and sync_ff1.
    always_ff @(posedge clk_b or negedge rst_n) begin
        if (!rst_n) begin
            sync_ff1 <= 1'b0;
            sync_ff2 <= 1'b0;
        end else begin
            sync_ff1 <= q_a;
            sync_ff2 <= sync_ff1;
        end
    end

endmodule
