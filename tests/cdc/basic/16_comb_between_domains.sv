// Test 16: Combinational logic sits between a clk_a FF and the FIRST
// stage of the destination 2-FF synchronizer. The Cummings rule
// (Ac_cdc02) requires the synchronizer's data input to come DIRECTLY
// from a source-domain flop -- combo logic in the path can glitch and
// the metastability filter is no longer trustworthy.
//
// Source FF (q_a) -> AND/OR -> sync_ff1 -> sync_ff2.
//
// Expected: 0 violations, 2 cautions (one per comb input,
//           rule=Ac_cdc02 / glitch risk), 0 infos, 2 crossings.
//
// detectReconvergence preserves a more specific rule already set by
// detectCombBeforeSync, so the precise diagnosis (Ac_cdc02) survives
// rather than being overwritten by the generic reconvergence label.

module comb_between_domains (
    input  logic clk_a,
    input  logic clk_b,
    input  logic rst_n,
    input  logic data_in,
    input  logic gate_in
);

    logic q_a, q_a_other;
    logic sync_ff1, sync_ff2;

    always_ff @(posedge clk_a or negedge rst_n) begin
        if (!rst_n) begin
            q_a       <= 1'b0;
            q_a_other <= 1'b0;
        end else begin
            q_a       <= data_in;
            q_a_other <= gate_in;
        end
    end

    // Combinational expression feeding the synchronizer first stage.
    // This is the Ac_cdc02 anti-pattern.
    logic comb_expr;
    assign comb_expr = q_a & q_a_other;

    always_ff @(posedge clk_b or negedge rst_n) begin
        if (!rst_n) begin
            sync_ff1 <= 1'b0;
            sync_ff2 <= 1'b0;
        end else begin
            sync_ff1 <= comb_expr;
            sync_ff2 <= sync_ff1;
        end
    end

endmodule
