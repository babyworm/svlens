module prim_sync_reqack_data(
    input logic clk_src_i,
    input logic clk_dst_i,
    input logic src_req_i,
    output logic src_ack_o,
    output logic dst_req_o,
    input logic dst_ack_i,
    input logic data_i,
    output logic data_o
);
    assign src_ack_o = dst_ack_i;
    assign dst_req_o = src_req_i;
    assign data_o = data_i;
endmodule

module named_reqack_top(input logic clk_a, clk_b, req_i, ack_i, data_i,
                        output logic ack_o, req_o, data_o);
    prim_sync_reqack_data u_sync(
        .clk_src_i(clk_a), .clk_dst_i(clk_b),
        .src_req_i(req_i), .src_ack_o(ack_o),
        .dst_req_o(req_o), .dst_ack_i(ack_i),
        .data_i(data_i), .data_o(data_o)
    );
    prim_sync_reqack_data u_same_domain(
        .clk_src_i(clk_a), .clk_dst_i(clk_a),
        .src_req_i(req_i), .src_ack_o(),
        .dst_req_o(), .dst_ack_i(ack_i),
        .data_i(data_i), .data_o()
    );
    prim_sync_reqack_data u_unconnected(
        .clk_src_i(clk_a), .clk_dst_i(clk_b),
        .src_req_i(req_i), .src_ack_o(),
        .dst_req_o(), .dst_ack_i(ack_i),
        .data_i(data_i), .data_o()
    );
endmodule
