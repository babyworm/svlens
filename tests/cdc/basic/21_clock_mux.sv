// Test 21: Glitchless clock mux selecting between clk_a and clk_b at
// runtime. The mux output drives a downstream flop. Real-world clock
// muxes use a small handshake state machine to avoid glitches at the
// switching moment, but for this fixture we model the simple BAD case
// (raw `assign clk_mux = sel ? clk_a : clk_b;`) so a CDC tool has the
// chance to flag it.
//
// Expected: 1 VIOLATION (Ac_cdc05 -- flop clock driven by combinational
// expression without a glitch-free clock mux primitive), 0 cautions,
// 0 infos, 1 crossing.
//
// The Ac_cdc05 detector in clock_tree::detectUnsafeCombClocks finds
// `assign clk_mux = sel ? clk_a : clk_b;` (ConditionalOp RHS on a
// signal that is also used as a flop clock) and marks the source as
// unsafe. CdcRunnerUtils then emits one Ac_cdc05 violation per flop in
// the unsafe domain. To suppress the rule on a project that uses a
// proven glitch-free mux primitive, register the cell name with
// --glitch-free-mux-cell <name> or --cdc-config <yaml>. To disable
// the rule entirely, pass --no-check-clock-mux. See fixtures 34 and
// 35 for the negative pairs.

module clock_mux (
    input  logic clk_a,
    input  logic clk_b,
    input  logic sel,
    input  logic rst_n,
    input  logic data_in
);

    // Combinational mux on clocks -- glitch hazard at switch.
    logic clk_mux;
    assign clk_mux = sel ? clk_a : clk_b;

    logic q;
    always_ff @(posedge clk_mux or negedge rst_n) begin
        if (!rst_n)
            q <= 1'b0;
        else
            q <= data_in;
    end

endmodule
