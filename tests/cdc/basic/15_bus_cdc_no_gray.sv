// Test 15: Multi-bit bus crossing clk_a -> clk_b via per-bit 2-FF
// synchronizers, WITHOUT gray-coding or handshake.
//
// Real-world hazard: each bit individually has a proper 2-FF sync, but
// bits resolve in unpredictable cycles -- a downstream consumer sees
// intermediate values until all bits settle. The textbook fix is to
// gray-code the bus before crossing or to gate the bus value with a
// single-bit handshake.
//
// CURRENT svlens BEHAVIOR (known limitation): the tool collapses the
// 4-bit bus crossing into a single INFO crossing. The Ac_cdc03
// reconvergence rule fires only on multiple distinct *signals*, not
// on multiple bits of the same signal, so wide-bus skew is not flagged.
//
// This fixture pins down the current behavior so a future enhancement
// (per-bit decomposition or a dedicated wide-bus-CDC rule) can be
// gated by an updated golden value. See feature-roadmap.md for the
// "bus-CDC without coordination" detection follow-up.
//
// Expected (current): 0 violations, 0 cautions, 1 info, 1 crossing.

module bus_cdc_no_gray (
    input  logic       clk_a,
    input  logic       clk_b,
    input  logic       rst_n,
    input  logic [3:0] data_in
);

    logic [3:0] data_a;
    logic [3:0] sync_ff1, sync_ff2;

    always_ff @(posedge clk_a or negedge rst_n) begin
        if (!rst_n)
            data_a <= 4'h0;
        else
            data_a <= data_in;
    end

    // Per-bit 2-FF sync, no gray coding.
    always_ff @(posedge clk_b or negedge rst_n) begin
        if (!rst_n) begin
            sync_ff1 <= 4'h0;
            sync_ff2 <= 4'h0;
        end else begin
            sync_ff1 <= data_a;
            sync_ff2 <= sync_ff1;
        end
    end

endmodule
