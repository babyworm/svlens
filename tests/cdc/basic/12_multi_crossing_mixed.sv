// Test 12: Multiple independent single-bit crossings between the SAME
// domain pair, plus one missing-sync violation. This fixture exercises
// two orthogonal detections in one design:
//
//   * Ac_cdc03 "reconvergence risk" -- when multiple independently-synced
//     bits cross from the same source domain to the same destination
//     domain, the relative arrival order is not bounded. Each properly
//     synced 2-FF crossing is downgraded INFO -> CAUTION.
//   * Ac_cdc01 "missing sync" -- the third bit is sampled directly.
//
// Expected: 1 violation (bit 3), 2 cautions (bits 1 and 2 via Ac_cdc03),
//           0 infos, 3 crossings total. Enumeration completeness test:
//           all three crossings must be listed.

module multi_crossing_mixed (
    input  logic clk_a,
    input  logic clk_b,
    input  logic rst_n,
    input  logic data1_in,
    input  logic data2_in,
    input  logic data3_in
);

    // ---- sources in domain A ----
    logic data1_a, data2_a, data3_a;

    always_ff @(posedge clk_a or negedge rst_n) begin
        if (!rst_n) begin
            data1_a <= 1'b0;
            data2_a <= 1'b0;
            data3_a <= 1'b0;
        end else begin
            data1_a <= data1_in;
            data2_a <= data2_in;
            data3_a <= data3_in;
        end
    end

    // ---- Crossing 1: proper 2-FF sync ----
    logic data1_sync_ff1, data1_sync_ff2;

    always_ff @(posedge clk_b or negedge rst_n) begin
        if (!rst_n) begin
            data1_sync_ff1 <= 1'b0;
            data1_sync_ff2 <= 1'b0;
        end else begin
            data1_sync_ff1 <= data1_a;
            data1_sync_ff2 <= data1_sync_ff1;
        end
    end

    // ---- Crossing 2: proper 2-FF sync ----
    logic data2_sync_ff1, data2_sync_ff2;

    always_ff @(posedge clk_b or negedge rst_n) begin
        if (!rst_n) begin
            data2_sync_ff1 <= 1'b0;
            data2_sync_ff2 <= 1'b0;
        end else begin
            data2_sync_ff1 <= data2_a;
            data2_sync_ff2 <= data2_sync_ff1;
        end
    end

    // ---- Crossing 3: DIRECT, NO SYNC (violation) ----
    logic data3_b;

    always_ff @(posedge clk_b or negedge rst_n) begin
        if (!rst_n)
            data3_b <= 1'b0;
        else
            data3_b <= data3_a;  // directly samples clk_a output -> VIOLATION
    end

endmodule
