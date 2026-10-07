module fifo_transfer_stage(input logic clk_i, rst_ni,
                           input logic [2:0] d_i, output logic [2:0] q_o);
    always_ff @(posedge clk_i or negedge rst_ni)
        if (!rst_ni) q_o <= '0;
        else q_o <= d_i;
endmodule

module fifo_transfer_sync(input logic clk_i, rst_ni,
                          input logic [2:0] d_i, output logic [2:0] q_o);
    logic [2:0] first;
    fifo_transfer_stage u_sync_1(.clk_i, .rst_ni, .d_i, .q_o(first));
    fifo_transfer_stage u_sync_2(.clk_i, .rst_ni, .d_i(first), .q_o);
endmodule

module prim_fifo_async(input logic clk_wr_i, clk_rd_i, rst_wr_ni, rst_rd_ni,
                       input logic wvalid_i, rready_i,
                       output logic wready_o, rvalid_o,
                       output logic [2:0] synced_wptr_o, synced_rptr_o);
    logic [2:0] fifo_wptr_q, fifo_rptr_q;
    logic [2:0] fifo_wptr_gray_q, fifo_rptr_gray_q;
    assign wready_o = !fifo_wptr_q[2];
    assign rvalid_o = fifo_wptr_q != fifo_rptr_q;
    always_ff @(posedge clk_wr_i or negedge rst_wr_ni) begin
        if (!rst_wr_ni) begin
            fifo_wptr_q <= '0;
            fifo_wptr_gray_q <= '0;
        end else if (wvalid_i && wready_o) begin
            fifo_wptr_q <= fifo_wptr_q + 3'd1;
            fifo_wptr_gray_q <= ((fifo_wptr_q + 3'd1) >> 1) ^ (fifo_wptr_q + 3'd1);
`ifdef SVLENS_FIFO_SVA_MUTATE
        end else begin
            fifo_wptr_gray_q <= fifo_wptr_gray_q ^ 3'b001;
`endif
        end
    end
    always_ff @(posedge clk_rd_i or negedge rst_rd_ni) begin
        if (!rst_rd_ni) begin
            fifo_rptr_q <= '0;
            fifo_rptr_gray_q <= '0;
        end else if (rvalid_o && rready_i) begin
            fifo_rptr_q <= fifo_rptr_q + 3'd1;
            fifo_rptr_gray_q <= ((fifo_rptr_q + 3'd1) >> 1) ^ (fifo_rptr_q + 3'd1);
        end
    end
    fifo_transfer_sync sync_wptr(.clk_i(clk_rd_i), .rst_ni(rst_rd_ni),
                                  .d_i(fifo_wptr_gray_q), .q_o(synced_wptr_o));
    fifo_transfer_sync sync_rptr(.clk_i(clk_wr_i), .rst_ni(rst_wr_ni),
                                  .d_i(fifo_rptr_gray_q), .q_o(synced_rptr_o));
endmodule

module fifo_transfer_sva_top(input logic clk_wr_i, clk_rd_i, rst_wr_ni, rst_rd_ni,
                             input logic wvalid_i, rready_i,
                             output logic wready_o, rvalid_o,
                             output logic [2:0] synced_wptr_o, synced_rptr_o);
    prim_fifo_async u_fifo(.*);
endmodule

module fifo_transfer_generated_top(input logic clk_wr_i, clk_rd_i, rst_wr_ni, rst_rd_ni,
                                   input logic wvalid_i, rready_i,
                                   output logic wready_o, rvalid_o,
                                   output logic [2:0] synced_wptr_o, synced_rptr_o);
    for (genvar lane = 0; lane < 1; lane++) begin : gen_lanes
        prim_fifo_async u_fifo(.*);
    end
endmodule

module fifo_transfer_partial_top(input logic clk_wr_i, clk_rd_i, rst_wr_ni, rst_rd_ni,
                                 input logic wvalid_i, rready_i,
                                 output logic rvalid_o,
                                 output logic [2:0] synced_wptr_o, synced_rptr_o);
    prim_fifo_async u_fifo(
        .clk_wr_i, .clk_rd_i, .rst_wr_ni, .rst_rd_ni,
        .wvalid_i, .wready_o(), .rready_i, .rvalid_o,
        .synced_wptr_o, .synced_rptr_o);
endmodule
