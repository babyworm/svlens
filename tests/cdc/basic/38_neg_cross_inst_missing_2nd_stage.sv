// Test 38 (paired NEGATIVE for fixture 33): cross-instance sync
// chain with only a SINGLE sub-flop instance -- the 2nd sync stage
// is MISSING. Detection must remain a VIOLATION even though
// `findNextFF` now walks across adjacent submodule instances.
//
// This fixture exists to guard against the sync_verifier
// relaxation in `findNextFF` over-classifying single-stage
// cross-instance flops as 2-FF synchronizers. Compare with
// fixture 33 which has TWO sub-flop instances and resolves to
// sync_type=two_ff.
//
// Expected: 1 violation, 0 cautions, 0 infos, 1 crossing
//           (data_src_q -> u_sync.u_only.q_o, sync_type=none).

module neg_cross_inst_missing_2nd_stage (
    input  logic       src_clk,
    input  logic       dst_clk,
    input  logic       rst_n,
    input  logic [3:0] data_in
);

    logic [3:0] data_src_q;

    always_ff @(posedge src_clk or negedge rst_n) begin
        if (!rst_n)
            data_src_q <= 4'h0;
        else
            data_src_q <= data_in;
    end

    // Sync wrapper but with ONLY ONE sub-flop -- this is the missing
    // 2nd-stage anti-pattern that real designs sometimes ship.
    sync_1flop_indirect u_sync (
        .clk_i  (dst_clk),
        .rst_ni (rst_n),
        .d_i    (data_src_q),
        .q_o    ()
    );

endmodule

module sync_1flop_indirect #(
    parameter int W = 4
) (
    input  logic         clk_i,
    input  logic         rst_ni,
    input  logic [W-1:0] d_i,
    output logic [W-1:0] q_o
);

    logic [W-1:0] d_o;
    always_comb d_o = d_i;

    flop_w_neg u_only (.clk_i, .rst_ni, .d_i(d_o), .q_o(q_o));

endmodule

module flop_w_neg #(
    parameter int W = 4
) (
    input  logic         clk_i,
    input  logic         rst_ni,
    input  logic [W-1:0] d_i,
    output logic [W-1:0] q_o
);

    always_ff @(posedge clk_i or negedge rst_ni) begin
        if (!rst_ni)
            q_o <= '0;
        else
            q_o <= d_i;
    end

endmodule
