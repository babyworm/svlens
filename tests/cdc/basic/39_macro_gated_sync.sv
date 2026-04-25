// Test 39 (positive): macro / include path workflow. The
// synchronizer is hidden behind a `SYNC_2FF` macro defined in a
// header that lives under tests/cdc/basic/inc/. The fixture
// requires the runner to pass -I tests/cdc/basic/inc and a -D
// SYNC_BACKEND_2FF token; this exercises the slang preprocessor
// pass-through path (CompilationSession.cpp) that downstream
// macro-heavy projects (VeeR-EH1, BlackParrot, OpenTitan) need.
//
// Expected: 0 violations, 0 cautions, 1 info, 1 crossing.

`include "macro_sync_chain.svh"

`ifndef SYNC_BACKEND_2FF
  `error "Run with -DSYNC_BACKEND_2FF to expand the proper sync"
`endif

module macro_gated_sync (
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

    logic [3:0] data_dst_q;
    `SYNC_2FF(u_sync, 4, data_src_q, data_dst_q, dst_clk, rst_n)

    // Downstream consumer in dst_clk domain (prevents dead-code
    // elimination of the sync chain).
    logic [3:0] data_dst_use;
    always_ff @(posedge dst_clk or negedge rst_n) begin
        if (!rst_n) data_dst_use <= '0;
        else        data_dst_use <= data_dst_q;
    end

endmodule
