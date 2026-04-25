// Test 29: Negative pair for Ac_cdc03 (reconvergence -- multiple
// independently-synced bits sharing a domain pair). Two crossings live
// in DISTINCT domain pairs (clk_a -> clk_b vs. clk_a -> clk_c), so the
// reconvergence rule does not fire even though the same source domain
// crosses twice.
//
// Mirror of fixture 12 (positive) and a more compact relative of
// fixture 13.
//
// Expected (current svlens behavior): 0 violations, 2 cautions, 0 infos,
//           2 crossings. Even though the two crossings have distinct
//           destination domains (so Ac_cdc03 reconvergence does NOT
//           fire), svlens flags both with rule Ac_cdc01 / category
//           CAUTION because the source signal `data_a` fans out to
//           multiple async domains. This pattern is the
//           "negative-for-Ac_cdc03 + positive-for-fan-out" combination,
//           which is faithfully reflected in the golden.

module neg_ac_cdc03_distinct_pairs (
    input  logic clk_a,
    input  logic clk_b,
    input  logic clk_c,
    input  logic rst_n,
    input  logic data_in
);

    logic data_a;
    logic b_sync_ff1, b_sync_ff2;
    logic c_sync_ff1, c_sync_ff2;

    always_ff @(posedge clk_a or negedge rst_n) begin
        if (!rst_n)
            data_a <= 1'b0;
        else
            data_a <= data_in;
    end

    always_ff @(posedge clk_b or negedge rst_n) begin
        if (!rst_n) begin
            b_sync_ff1 <= 1'b0;
            b_sync_ff2 <= 1'b0;
        end else begin
            b_sync_ff1 <= data_a;
            b_sync_ff2 <= b_sync_ff1;
        end
    end

    always_ff @(posedge clk_c or negedge rst_n) begin
        if (!rst_n) begin
            c_sync_ff1 <= 1'b0;
            c_sync_ff2 <= 1'b0;
        end else begin
            c_sync_ff1 <= data_a;
            c_sync_ff2 <= c_sync_ff1;
        end
    end

endmodule
