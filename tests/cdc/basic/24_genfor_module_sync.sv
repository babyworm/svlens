// Test 24: Multi-bit pointer crossing where the destination side wraps
// each bit's 2-FF synchronizer in its own MODULE INSTANCE inside a
// generate-for. This mirrors the exact pulp-platform/common_cells
// cdc_fifo_gray idiom:
//
//   for (genvar i = 0; i < PTR_W; i++) begin : gen_sync
//     sync u_sync (.serial_i(async_ptr[i]), .serial_o(synced[i]));
//   end
//
// Expected (after the genfor + module-instance fix): 0 violations,
//           4 cautions (one per bit of wptr_q via Ac_cdc03 reconvergence
//           because all bits share src_clk -> dst_clk), 0 infos,
//           4 crossings. The connectivity tracker walks the
//           generate-for, enters each bit's sync_2ff submodule
//           instance, and resolves the bit-select port connection
//           back to the multi-bit register in the enclosing scope.

module genfor_module_sync (
    input  logic       src_clk,
    input  logic       dst_clk,
    input  logic       rst_n,
    input  logic [3:0] data_in,
    output logic [3:0] data_out
);

    logic [3:0] wptr_q;
    logic [3:0] synced;

    always_ff @(posedge src_clk or negedge rst_n) begin
        if (!rst_n)
            wptr_q <= 4'h0;
        else
            wptr_q <= data_in;
    end

    for (genvar i = 0; i < 4; i++) begin : gen_sync
        sync_2ff u_sync (
            .clk_dst_i (dst_clk),
            .rst_ni    (rst_n),
            .d_src_i   (wptr_q[i]),
            .q_dst_o   (synced[i])
        );
    end

    assign data_out = synced;

endmodule

module sync_2ff (
    input  logic clk_dst_i,
    input  logic rst_ni,
    input  logic d_src_i,
    output logic q_dst_o
);

    logic ff1, ff2;

    always_ff @(posedge clk_dst_i or negedge rst_ni) begin
        if (!rst_ni) begin
            ff1 <= 1'b0;
            ff2 <= 1'b0;
        end else begin
            ff1 <= d_src_i;
            ff2 <= ff1;
        end
    end

    assign q_dst_o = ff2;

endmodule
