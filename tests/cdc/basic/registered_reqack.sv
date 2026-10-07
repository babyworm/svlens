module reqack_stage(input logic clk_i, rst_ni, d_i, output logic q_o);
    always_ff @(posedge clk_i or negedge rst_ni)
        if (!rst_ni) q_o <= 1'b0;
        else q_o <= d_i;
endmodule

module reqack_two_sync(input logic clk_i, rst_ni, d_i, output logic q_o);
    logic first;
    reqack_stage u_sync_1(.clk_i, .rst_ni, .d_i, .q_o(first));
    reqack_stage u_sync_2(.clk_i, .rst_ni, .d_i(first), .q_o);
endmodule

module prim_sync_reqack(input logic clk_src_i, rst_src_ni, clk_dst_i, rst_dst_ni,
                        input logic src_req_i, dst_ack_i,
                        output logic src_ack_o, dst_req_o);
    logic src_req_q, dst_ack_q;
    always_ff @(posedge clk_src_i or negedge rst_src_ni)
        if (!rst_src_ni) src_req_q <= 1'b0;
        else src_req_q <= src_req_i;
    reqack_two_sync req_sync(.clk_i(clk_dst_i), .rst_ni(rst_dst_ni),
                             .d_i(src_req_q), .q_o(dst_req_o));

    always_ff @(posedge clk_dst_i or negedge rst_dst_ni)
        if (!rst_dst_ni) dst_ack_q <= 1'b0;
        else dst_ack_q <= dst_ack_i;
    reqack_two_sync ack_sync(.clk_i(clk_src_i), .rst_ni(rst_src_ni),
                             .d_i(dst_ack_q), .q_o(src_ack_o));
endmodule

module reqack_top(input logic clk_src_i, rst_src_ni, clk_dst_i, rst_dst_ni,
                  input logic src_req_i, dst_ack_i,
                  output logic src_ack_o, dst_req_o);
    prim_sync_reqack u_reqack(.*);
endmodule
