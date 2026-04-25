// Test 13: Three-domain chain, A -> B -> C, each hop uses a proper
// 2-FF synchronizer. Crossings live in DISTINCT domain pairs so the
// Ac_cdc03 reconvergence rule does not fire. This is the pure
// enumeration test: the tool must list both crossings as INFO.
//
// Expected: 0 violations, 0 cautions, 2 infos, 2 crossings
//           (clk_a -> clk_b, clk_b -> clk_c).

module three_domain_chain (
    input  logic clk_a,
    input  logic clk_b,
    input  logic clk_c,
    input  logic rst_n,
    input  logic data_in
);

    logic data_a;
    logic a_sync_ff1, a_sync_ff2;  // clk_b domain
    logic b_sync_ff1, b_sync_ff2;  // clk_c domain

    // ---- domain A source ----
    always_ff @(posedge clk_a or negedge rst_n) begin
        if (!rst_n)
            data_a <= 1'b0;
        else
            data_a <= data_in;
    end

    // ---- hop 1: A -> B ----
    always_ff @(posedge clk_b or negedge rst_n) begin
        if (!rst_n) begin
            a_sync_ff1 <= 1'b0;
            a_sync_ff2 <= 1'b0;
        end else begin
            a_sync_ff1 <= data_a;
            a_sync_ff2 <= a_sync_ff1;
        end
    end

    // ---- hop 2: B -> C ----
    always_ff @(posedge clk_c or negedge rst_n) begin
        if (!rst_n) begin
            b_sync_ff1 <= 1'b0;
            b_sync_ff2 <= 1'b0;
        end else begin
            b_sync_ff1 <= a_sync_ff2;
            b_sync_ff2 <= b_sync_ff1;
        end
    end

endmodule
