// Test 14: Crossing where the destination has ONLY ONE flop in the
// destination domain, not a 2-FF synchronizer pair. This is the
// minimum-insufficient synchronizer -- metastability can propagate to
// downstream logic from a single stage.
//
// Expected: 1 violation, 0 cautions, 0 infos, 1 crossing with
//           sync_type != two_ff / three_ff (either "one_ff" or "none"
//           depending on tool vocabulary).

module single_ff_only (
    input  logic clk_a,
    input  logic clk_b,
    input  logic rst_n,
    input  logic data_in,
    output logic data_out
);

    logic q_a;
    logic q_b;  // single stage sampling q_a directly -- insufficient

    always_ff @(posedge clk_a or negedge rst_n) begin
        if (!rst_n)
            q_a <= 1'b0;
        else
            q_a <= data_in;
    end

    always_ff @(posedge clk_b or negedge rst_n) begin
        if (!rst_n)
            q_b <= 1'b0;
        else
            q_b <= q_a;   // only one FF in dest domain -> weak sync
    end

    // IMPORTANT: q_b is consumed combinationally by downstream logic
    // (assign), so metastability propagates. With no second stage, this
    // is a real CDC hazard.
    assign data_out = q_b;

endmodule
