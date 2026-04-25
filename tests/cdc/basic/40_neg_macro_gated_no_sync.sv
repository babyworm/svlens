// Test 40 (paired NEGATIVE for fixture 39): same macro / include
// workflow but with a `SYNC_DIRECT` backend that omits the
// metastable stage -- the data_src_q reg drives the destination
// flop with NO synchronizer. Detection must produce a VIOLATION
// even though the sync hides behind a `define just like in
// fixture 39.
//
// Expected: 1 violation, 0 cautions, 0 infos, 1 crossing.

`include "macro_sync_chain.svh"

`ifndef SYNC_BACKEND_DIRECT
  `error "Run with -DSYNC_BACKEND_DIRECT to expand the missing-sync path"
`endif

`define SYNC_DIRECT(name, w, src, dst, clk, rstn) \
    logic [w-1:0] name``_q; \
    always_ff @(posedge clk or negedge rstn) begin \
        if (!rstn) name``_q <= '0; \
        else       name``_q <= src; \
    end \
    assign dst = name``_q;

module neg_macro_gated_no_sync (
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
    `SYNC_DIRECT(u_no_sync, 4, data_src_q, data_dst_q, dst_clk, rst_n)

    logic [3:0] data_dst_use;
    always_ff @(posedge dst_clk or negedge rst_n) begin
        if (!rst_n) data_dst_use <= '0;
        else        data_dst_use <= data_dst_q;
    end

endmodule
