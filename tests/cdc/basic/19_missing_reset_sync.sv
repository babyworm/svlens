// Test 19: Internal reset CDC WITHOUT a 2-FF deassert synchronizer.
// rst_gen_q is generated on clk_a and wired DIRECTLY to the async reset
// port of a clk_b flop. The deassert edge is not aligned with clk_b, so
// the consumer can come out of reset on a metastable edge.
//
// This is the missing-sync sister of fixture 18. The fact that the
// reset signal is asynchronously consumed (sensitivity list edge) is
// what `detectResetSyncIssues` (Ac_cdc06) is built to catch.
//
// Expected: at least one CAUTION on rst_gen_q -> consumer_q with
//           rule Ac_cdc06. crossing count >= 1.

module missing_reset_sync (
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

    // Direct cross-domain async reset usage -- VIOLATION:
    // rst_gen_q crosses clk_a -> clk_b without a 2-FF deassert chain.
    logic consumer_q;

    always_ff @(posedge clk_b or negedge rst_gen_q) begin
        if (!rst_gen_q)
            consumer_q <= 1'b0;
        else
            consumer_q <= ~consumer_q;
    end

    assign q_out = consumer_q;

endmodule
