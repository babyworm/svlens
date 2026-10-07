module fifo_gray_stage(input logic clk_i, rst_ni,
                       input logic [2:0] d_i, output logic [2:0] q_o);
    always_ff @(posedge clk_i or negedge rst_ni)
        if (!rst_ni) q_o <= '0;
        else q_o <= d_i;
endmodule

module fifo_gray_sync(input logic clk_i, rst_ni,
                      input logic [2:0] d_i, output logic [2:0] q_o);
    logic [2:0] first;
    fifo_gray_stage u_sync_1(.clk_i, .rst_ni, .d_i, .q_o(first));
    fifo_gray_stage u_sync_2(.clk_i, .rst_ni, .d_i(first), .q_o);
endmodule

module prim_fifo_async(input logic clk_wr_i, clk_rd_i, rst_wr_ni, rst_rd_ni,
                       output logic [2:0] synced_gray_o);
    logic [2:0] fifo_wptr_q, fifo_wptr_gray_q;
    logic [2:0] fifo_wptr_next;
    assign fifo_wptr_next = fifo_wptr_q + 3'd1;
    always_ff @(posedge clk_wr_i or negedge rst_wr_ni) begin
        if (!rst_wr_ni) begin
            fifo_wptr_q <= '0;
            fifo_wptr_gray_q <= '0;
        end else begin
            fifo_wptr_q <= fifo_wptr_next;
            fifo_wptr_gray_q <= (fifo_wptr_next >> 1) ^ fifo_wptr_next;
        end
    end
    fifo_gray_sync sync_wptr(.clk_i(clk_rd_i), .rst_ni(rst_rd_ni),
                             .d_i(fifo_wptr_gray_q), .q_o(synced_gray_o));
endmodule

module fifo_gray_top(input logic clk_wr_i, clk_rd_i, rst_wr_ni, rst_rd_ni,
                     output logic [2:0] synced_gray_o);
    for (genvar lane = 0; lane < 1; lane++) begin : gen_lanes
        if (lane == 0) begin : gen_fifo
            prim_fifo_async u_fifo(.*);
        end
    end
endmodule
