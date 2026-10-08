module prim_sync_reqack_data #(
    parameter int unsigned Width = 4,
    parameter bit DataSrc2Dst = 1'b1,
    parameter bit DataReg = 1'b0,
    parameter bit EnReqStabA = 1'b1
) (
    input logic clk_src_i, rst_src_ni, clk_dst_i, rst_dst_ni,
    input logic src_req_i,
    output logic src_ack_o, dst_req_o,
    input logic dst_ack_i,
    input logic [Width-1:0] data_i,
    output logic [Width-1:0] data_o
);
    assign src_ack_o = dst_ack_i;
    assign dst_req_o = src_req_i;
    assign data_o = data_i;
endmodule

module reqack_data_sva_top(input logic clk_a, clk_b, rst_a_n, rst_b_n,
                           req_i, ack_i, input logic [3:0] data_ab, data_ba,
                           output logic [3:0] out_ab, out_ba);
    logic ack_ab, req_ab, ack_ba, req_ba, ack_buf, req_buf, ack_partial, req_partial;
    logic [3:0] unused_buffered;

    prim_sync_reqack_data #(.Width(4)) u_forward(
        .clk_src_i(clk_a), .rst_src_ni(rst_a_n), .clk_dst_i(clk_b), .rst_dst_ni(rst_b_n),
        .src_req_i(req_i), .src_ack_o(ack_ab), .dst_req_o(req_ab), .dst_ack_i(ack_i),
        .data_i(data_ab), .data_o(out_ab));

    prim_sync_reqack_data #(.Width(4), .DataSrc2Dst(1'b0)) u_reverse(
        .clk_src_i(clk_a), .rst_src_ni(rst_a_n), .clk_dst_i(clk_b), .rst_dst_ni(rst_b_n),
        .src_req_i(req_i), .src_ack_o(ack_ba), .dst_req_o(req_ba), .dst_ack_i(ack_i),
        .data_i(data_ba), .data_o(out_ba));

    prim_sync_reqack_data #(.Width(4), .DataSrc2Dst(1'b0), .DataReg(1'b1)) u_buffered(
        .clk_src_i(clk_a), .rst_src_ni(rst_a_n), .clk_dst_i(clk_b), .rst_dst_ni(rst_b_n),
        .src_req_i(req_i), .src_ack_o(ack_buf), .dst_req_o(req_buf), .dst_ack_i(ack_i),
        .data_i(data_ba), .data_o(unused_buffered));

    prim_sync_reqack_data #(.Width(4)) u_partial(
        .clk_src_i(clk_a), .rst_src_ni(rst_a_n), .clk_dst_i(clk_b), .rst_dst_ni(rst_b_n),
        .src_req_i(req_i), .src_ack_o(ack_partial), .dst_req_o(req_partial), .dst_ack_i(ack_i),
        .data_i(data_ab), .data_o());
endmodule

module reqack_data_forward_sva_top(input logic clk_a, clk_b, rst_a_n, rst_b_n,
                                   input logic req_i, ack_i,
                                   input logic [3:0] data_ab,
                                   output logic [3:0] out_ab);
    logic src_ack, dst_req;
    prim_sync_reqack_data #(.Width(4)) u_forward(
        .clk_src_i(clk_a), .rst_src_ni(rst_a_n),
        .clk_dst_i(clk_b), .rst_dst_ni(rst_b_n),
        .src_req_i(req_i), .src_ack_o(src_ack),
        .dst_req_o(dst_req), .dst_ack_i(ack_i),
        .data_i(data_ab), .data_o(out_ab));
endmodule
