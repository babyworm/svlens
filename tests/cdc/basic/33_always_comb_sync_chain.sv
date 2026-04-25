// Test 33: Sync wrapper that uses an internal always_comb / assign to
// propagate the input port to an internal wire which then feeds a
// chained sub-flop. Mirrors the pattern in
// lowRISC/opentitan/hw/ip/prim_generic/rtl/prim_flop_2sync.sv:
//
//   always_comb d_o = d_i;          // internal wire driven by input port
//   prim_flop u_sync_1 (.d_i(d_o), .q_o(intq));
//   prim_flop u_sync_2 (.d_i(intq), .q_o(q_o));
//
// Expected: 1 violation, 0 cautions, 0 infos, 1 crossing
//           (data_src_q -> u_sync.u_sync_1.q_o).
//
// The connectivity tracker now chases `always_comb d_o = d_i;`
// across submodule boundaries, so the chain from data_src_q (on
// src_clk) into the first sub-flop u_sync_1.q_o (on dst_clk)
// resolves end-to-end.
//
// The disposition is VIOLATION rather than INFO because the second
// sync stage (u_sync_2) is a SEPARATE flop_w submodule instance, and
// detectSyncPattern's `findNextFF` does not yet trace across
// adjacent module instances at the dst-domain side. Codify the
// current behaviour; a future sync_verifier enhancement to walk
// adjacent submodule instances should flip this back to INFO.

module always_comb_sync_chain (
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

    sync_2flop_indirect u_sync (
        .clk_i  (dst_clk),
        .rst_ni (rst_n),
        .d_i    (data_src_q),
        .q_o    ()
    );

endmodule

module sync_2flop_indirect #(
    parameter int W = 4
) (
    input  logic         clk_i,
    input  logic         rst_ni,
    input  logic [W-1:0] d_i,
    output logic [W-1:0] q_o
);

    // Indirect propagation: input port d_i feeds an internal wire d_o
    // through always_comb (semantically equivalent to a continuous
    // assign), then d_o feeds a sub-flop's d_i input.
    logic [W-1:0] d_o;
    always_comb d_o = d_i;

    logic [W-1:0] intq;

    flop_w u_sync_1 (.clk_i, .rst_ni, .d_i(d_o), .q_o(intq));
    flop_w u_sync_2 (.clk_i, .rst_ni, .d_i(intq), .q_o(q_o));

endmodule

module flop_w #(
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
