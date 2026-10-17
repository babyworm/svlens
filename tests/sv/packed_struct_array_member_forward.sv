typedef struct packed {
    logic valid;
    logic [7:0] data;
} member_req_t;

module member_forward_source(output logic [7:0] data_o);
    assign data_o = 8'hA5;
endmodule

module member_forward_sink(input logic [7:0] data_i);
endmodule

module member_forward_valid_source(output logic valid_o);
    assign valid_o = 1'b1;
endmodule

module member_forward_valid_sink(input logic valid_i);
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

module packed_struct_array_member_dynamic_top(input logic select_i);
    logic [7:0] source0, source1, selected_data;
    member_req_t [1:0] reqs;
    member_forward_source u_source0(.data_o(source0));
    member_forward_source u_source1(.data_o(source1));
    assign reqs[0].data = source0;
    assign reqs[1].data = source1;
    assign selected_data = reqs[select_i].data;
    member_forward_sink u_sink(.data_i(selected_data));
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
