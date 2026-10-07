module enabled_sync_stage(input logic clk_src, clk_dst,
                          input logic rst_src_ni, rst_dst_ni,
                          input logic enable_i, data_i,
                          output logic data_o);
    logic source_q, first_q, second_q;
    always_ff @(posedge clk_src or negedge rst_src_ni)
        if (!rst_src_ni) source_q <= 1'b0;
        else source_q <= data_i;
    always_ff @(posedge clk_dst or negedge rst_dst_ni)
        if (!rst_dst_ni) first_q <= 1'b0;
        else first_q <= source_q;
    always_ff @(posedge clk_dst or negedge rst_dst_ni)
        if (!rst_dst_ni) second_q <= 1'b0;
        else if (enable_i) second_q <= first_q;
    assign data_o = second_q;
endmodule
