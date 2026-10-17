typedef struct packed {
    logic valid;
    logic [7:0] data;
} member_req_t;

module member_forward_source(output logic [7:0] data_o);
    assign data_o = 8'hA5;
endmodule

module member_forward_sink(input logic [7:0] data_i);
endmodule

module member_forward_nibble_sink(input logic [3:0] data_i);
endmodule

module member_forward_valid_source(output logic valid_o);
    assign valid_o = 1'b1;
endmodule

module member_forward_valid_sink(input logic valid_i);
endmodule

module plain_struct_forward(input member_req_t req_i);
    member_forward_sink u_data(.data_i(req_i.data));
    member_forward_nibble_sink u_high(.data_i(req_i.data[7:4]));
    member_forward_nibble_sink u_low(.data_i(req_i.data[3:0]));
    member_forward_valid_sink u_bit(.valid_i(req_i.data[5]));
    member_forward_valid_sink u_valid(.valid_i(req_i.valid));
endmodule

module plain_struct_port_forward_top;
    logic [7:0] source_data;
    logic source_valid;
    member_req_t req;
    member_forward_source u_data(.data_o(source_data));
    member_forward_valid_source u_valid(.valid_o(source_valid));
    assign req.data = source_data;
    assign req.valid = source_valid;
    plain_struct_forward u_forward(.req_i(req));
endmodule

typedef struct packed {
    logic valid;
    logic [0:7] data;
} member_req_ascending_t;

module plain_struct_ascending_forward(input member_req_ascending_t req_i);
    member_forward_nibble_sink u_high(.data_i(req_i.data[0:3]));
    member_forward_nibble_sink u_low(.data_i(req_i.data[4:7]));
    member_forward_valid_sink u_bit(.valid_i(req_i.data[2]));
endmodule

module plain_struct_ascending_top;
    logic [7:0] source_data;
    member_req_ascending_t req;
    member_forward_source u_data(.data_o(source_data));
    assign req.data = source_data;
    assign req.valid = 1'b1;
    plain_struct_ascending_forward u_forward(.req_i(req));
endmodule

module member_forward_desc(input member_req_t [1:0] req_i);
    member_forward_sink u_sink0(.data_i(req_i[0].data));
    member_forward_sink u_sink1(.data_i(req_i[1].data));
    member_forward_valid_sink u_vsink0(.valid_i(req_i[0].valid));
    member_forward_valid_sink u_vsink1(.valid_i(req_i[1].valid));
endmodule

module member_forward_asc(input member_req_t [0:1] req_i);
    member_forward_sink u_sink0(.data_i(req_i[0].data));
    member_forward_sink u_sink1(.data_i(req_i[1].data));
    member_forward_valid_sink u_vsink0(.valid_i(req_i[0].valid));
    member_forward_valid_sink u_vsink1(.valid_i(req_i[1].valid));
endmodule

module packed_struct_array_member_top;
    logic [7:0] source0, source1;
    logic valid0, valid1;
    member_req_t [1:0] reqs;
    member_forward_source u_source0(.data_o(source0));
    member_forward_source u_source1(.data_o(source1));
    member_forward_valid_source u_valid0(.valid_o(valid0));
    member_forward_valid_source u_valid1(.valid_o(valid1));
    assign reqs[0].data = source0;
    assign reqs[1].data = source1;
    assign reqs[0].valid = valid0;
    assign reqs[1].valid = valid1;
    member_forward_desc u_desc(.req_i(reqs));
    member_forward_asc u_asc(.req_i(reqs));
endmodule

module packed_struct_array_member_dynamic_top;
    logic [7:0] source0, source1, selected_data;
    logic select_i, valid0, valid1, selected_valid;
    member_req_t [1:0] reqs;
    member_forward_source u_source0(.data_o(source0));
    member_forward_source u_source1(.data_o(source1));
    member_forward_valid_source u_select(.valid_o(select_i));
    member_forward_valid_source u_valid0(.valid_o(valid0));
    member_forward_valid_source u_valid1(.valid_o(valid1));
    assign reqs[0].data = source0;
    assign reqs[1].data = source1;
    assign reqs[0].valid = valid0;
    assign reqs[1].valid = valid1;
    assign selected_data = reqs[select_i].data;
    assign selected_valid = reqs[select_i].valid;
    member_forward_sink u_sink(.data_i(selected_data));
    member_forward_valid_sink u_vsink(.valid_i(selected_valid));
    member_forward_sink u_port_sink(.data_i(reqs[select_i].data));
    member_forward_valid_sink u_port_vsink(.valid_i(reqs[select_i].valid));
endmodule

module packed_vector_dynamic_top;
    logic [7:0] source0, source1, selected_data;
    logic select_i;
    logic [1:0][7:0] lanes;
    member_forward_source u_source0(.data_o(source0));
    member_forward_source u_source1(.data_o(source1));
    member_forward_valid_source u_select(.valid_o(select_i));
    assign lanes[0] = source0;
    assign lanes[1] = source1;
    assign selected_data = lanes[select_i];
    member_forward_sink u_sink(.data_i(selected_data));
endmodule

module packed_vector_dynamic_port_top;
    logic [7:0] source0, source1;
    logic select_i;
    logic [1:0][7:0] lanes;
    member_forward_source u_source0(.data_o(source0));
    member_forward_source u_source1(.data_o(source1));
    member_forward_valid_source u_select(.valid_o(select_i));
    assign lanes[0] = source0;
    assign lanes[1] = source1;
    member_forward_sink u_sink(.data_i(lanes[select_i]));
endmodule

typedef struct packed {
    logic [7:0] addr;
    logic [2:0] prot;
} member_aw_t;

typedef struct packed {
    member_aw_t aw;
    logic aw_valid;
} member_nested_req_t;

module member_nested_decode(input logic [7:0] addr_i);
endmodule

module member_nested_forward(input member_nested_req_t [1:0] req_i);
    member_nested_decode u_decode0(.addr_i(req_i[0].aw.addr));
    member_nested_decode u_decode1(.addr_i(req_i[1].aw.addr));
    member_forward_nibble_sink u_high0(.data_i(req_i[0].aw.addr[7:4]));
    member_forward_nibble_sink u_low1(.data_i(req_i[1].aw.addr[3:0]));
endmodule

module packed_nested_struct_array_member_top;
    logic [7:0] source0, source1;
    member_nested_req_t [1:0] reqs;
    member_forward_source u_source0(.data_o(source0));
    member_forward_source u_source1(.data_o(source1));
    assign reqs[0].aw.addr = source0;
    assign reqs[1].aw.addr = source1;
    assign reqs[0].aw.prot = '0;
    assign reqs[1].aw.prot = '0;
    assign reqs[0].aw_valid = 1'b1;
    assign reqs[1].aw_valid = 1'b1;
    member_nested_forward u_forward(.req_i(reqs));
endmodule
