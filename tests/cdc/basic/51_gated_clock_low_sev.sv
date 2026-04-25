// Test 51 (positive): gated-clock crossing produces Severity::Low
// (INFO category) with the recommendation "verify clock gating
// is safe". Used by the [ignore_gated] integration test to
// confirm that --ignore-gated correctly filters Low-severity
// entries from the report.
//
// The companion `.sdc` declares gated_clk as a generated clock
// from ca (NOT asynchronous), so the detector classifies the
// crossing as `is_gated && !is_async` per
// crossing_detector.cpp:240. Without SDC the crossing would
// fall into the asynchronous branch (high VIOLATION) instead.
//
// Expected (without --ignore-gated): 0 violations, 0 cautions,
//                                    1 info, 1 crossing.

module gated_clock_low_sev (
    input  logic ca,
    input  logic en,
    input  logic rst_n,
    input  logic d
);

    logic q_a;
    always_ff @(posedge ca or negedge rst_n)
        if (!rst_n) q_a <= 1'b0; else q_a <= d;

    // ICG cell: name `clk_gate` triggers isICGName detection.
    logic gated_clk;
    clk_gate u_icg (.clk_in(ca), .en(en), .clk_out(gated_clk));

    logic q_b;
    always_ff @(posedge gated_clk or negedge rst_n)
        if (!rst_n) q_b <= 1'b0; else q_b <= q_a;

endmodule

module clk_gate (input logic clk_in, en, output logic clk_out);
    assign clk_out = clk_in & en;
endmodule
