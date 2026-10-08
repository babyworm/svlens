module fifo_transfer_sva_tb;
    logic clk_wr_i = 1'b0;
    logic clk_rd_i = 1'b0;
    logic rst_wr_ni = 1'b0;
    logic rst_rd_ni = 1'b0;
    logic wvalid_i = 1'b0;
    logic rready_i = 1'b0;
    logic wready_o, rvalid_o;
    logic [2:0] synced_wptr_o, synced_rptr_o;

    fifo_transfer_sva_top fifo_transfer_sva_top(.*);
    svlens_cdc_assertions checks();

    always #5 clk_wr_i = ~clk_wr_i;
    always #7 clk_rd_i = ~clk_rd_i;

    initial begin
        #20;
        rst_wr_ni = 1'b1;
        rst_rd_ni = 1'b1;
        #100 $finish;
    end
endmodule
