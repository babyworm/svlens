// Test 20: One source FF (data_a on clk_a) fans out to TWO destination
// domains -- clk_b uses a proper 2-FF synchronizer, clk_c samples
// directly without sync. The same source signal participates in two
// crossings of different quality.
//
// Why this matters: many CDC tools collapse "same source, multiple
// destinations" into a single crossing report and then misclassify
// the worst-case violation. svlens must list each destination's
// crossing independently.
//
// CURRENT svlens BEHAVIOR: the missing-sync side (clk_c) is VIOLATION
// (Ac_cdc01). The properly-synced side (clk_b) is CAUTION via
// Ac_cdc11 "signal crosses to multiple clock domains independently"
// -- svlens correctly recognises the source FF fan-out pattern even
// when each destination has its own sync. Enumeration completeness
// is verified by the two reported crossings.
//
// Expected: 1 violation (clk_c, no sync),
//           1 caution  (clk_b side fan-out flagged for review),
//           0 infos,
//           2 crossings (enumeration completeness).

module fanout_mixed_sync (
    input  logic clk_a,
    input  logic clk_b,
    input  logic clk_c,
    input  logic rst_n,
    input  logic data_in
);

    logic data_a;

    always_ff @(posedge clk_a or negedge rst_n) begin
        if (!rst_n)
            data_a <= 1'b0;
        else
            data_a <= data_in;
    end

    // ---- destination clk_b: proper 2-FF sync ----
    logic b_sync_ff1, b_sync_ff2;

    always_ff @(posedge clk_b or negedge rst_n) begin
        if (!rst_n) begin
            b_sync_ff1 <= 1'b0;
            b_sync_ff2 <= 1'b0;
        end else begin
            b_sync_ff1 <= data_a;
            b_sync_ff2 <= b_sync_ff1;
        end
    end

    // ---- destination clk_c: DIRECT, no sync ----
    logic q_c;

    always_ff @(posedge clk_c or negedge rst_n) begin
        if (!rst_n)
            q_c <= 1'b0;
        else
            q_c <= data_a;   // VIOLATION: clk_a -> clk_c without sync
    end

endmodule
