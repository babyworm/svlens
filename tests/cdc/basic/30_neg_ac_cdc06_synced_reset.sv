// Test 30: Negative pair for Ac_cdc06 (async reset CDC without
// synchronizer). Mirror of fixture 19_missing_reset_sync but with a
// proper 2-FF reset deassert chain on the clk_b side. Must NOT raise
// Ac_cdc06.
//
// Expected: 0 violations, 0 cautions, 1 INFO (synced reset path),
//           1 crossing.

module neg_ac_cdc06_synced_reset (
    input  logic clk_a,
    input  logic clk_b,
    input  logic por_n,
    input  logic halt_req,
    output logic q_out
);

    logic rst_gen_q;

    always_ff @(posedge clk_a or negedge por_n) begin
        if (!por_n)
            rst_gen_q <= 1'b0;
        else
            rst_gen_q <= halt_req;
    end

    // Proper 2-FF deassert chain on the destination side.
    logic rst_b_sync_ff1, rst_b_sync_ff2;

    always_ff @(posedge clk_b or negedge por_n) begin
        if (!por_n) begin
            rst_b_sync_ff1 <= 1'b0;
            rst_b_sync_ff2 <= 1'b0;
        end else begin
            rst_b_sync_ff1 <= rst_gen_q;
            rst_b_sync_ff2 <= rst_b_sync_ff1;
        end
    end

    logic consumer_q;

    always_ff @(posedge clk_b or negedge por_n) begin
        if (!por_n)
            consumer_q <= 1'b0;
        else if (rst_b_sync_ff2)
            consumer_q <= 1'b0;
        else
            consumer_q <= ~consumer_q;
    end

    assign q_out = consumer_q;

endmodule
