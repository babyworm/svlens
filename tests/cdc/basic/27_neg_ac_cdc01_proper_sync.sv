// Test 27: Negative pair for Ac_cdc01 (missing 2-FF synchronizer).
// Structurally similar to fixture 02_missing_sync but with a proper
// 2-FF synchronizer in place. Must NOT raise Ac_cdc01.
//
// Naming the modules after fixture 02 makes the pairing intent clear:
// the issue is the only delta between the two designs.
//
// Expected: 0 violations, 0 cautions, 1 INFO, 1 crossing.

module neg_ac_cdc01_proper_sync (
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

    // Proper 2-FF sync (the differentiator vs. fixture 02).
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
