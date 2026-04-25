// Test 15: Multi-bit bus crossing clk_a -> clk_b via per-bit 2-FF
// synchronizers, WITHOUT gray-coding or handshake.
//
// Real-world hazard: each bit individually has a proper 2-FF sync, but
// bits resolve in unpredictable cycles -- a downstream consumer sees
// intermediate values until all bits settle. The textbook fix is to
// gray-code the bus before crossing or to gate the bus value with a
// single-bit handshake.
//
// Expected: 0 violations, 1 caution (Ac_cdc04 wide-bus crossing
//           without gray code or handshake), 0 infos, 1 crossing.
//
// Resolved by the new Ac_cdc04 detector in sync_verifier: a multi-bit
// register crossing through a plain TwoFF/ThreeFF chain produces a
// CAUTION because per-bit metastability skew at the sync output can
// momentarily expose intermediate values. The corresponding clean
// pattern (single-bit, or gray-coded, or handshake-gated) does NOT
// trip the rule -- see fixtures 03, 08, 07 plus the dedicated
// negative pair 32_neg_ac_cdc04_single_bit.

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
