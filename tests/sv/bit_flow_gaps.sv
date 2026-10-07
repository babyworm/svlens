module bit_flow_wide_source(output logic [4096:0] o_data);
    assign o_data = '0;
endmodule

module bit_flow_wide_sink(input logic [4096:0] i_data);
endmodule

module bit_flow_bit_source(output logic [7:0] o_data);
    assign o_data = 8'hA5;
endmodule

module bit_flow_bit_sink(input logic i_data);
endmodule

module bit_flow_gaps_top(input logic [2:0] i_index);
    logic [4096:0] wide_src;
    logic [4096:0] wide_dst;
    logic [7:0] bits;
    logic selected;
    logic [5:0] repeated;
    logic [1:0] tied;

    bit_flow_wide_source u_wide_source(.o_data(wide_src));
    assign {wide_dst[4096:1], wide_dst[0]} = {wide_src[4096:1], wide_src[0]};
    bit_flow_wide_sink u_wide_sink(.i_data(wide_dst));

    bit_flow_bit_source u_bit_source(.o_data(bits));
    assign selected = bits[i_index];
    for (genvar lane = 0; lane < 6; lane++) begin : gen_gaps
        assign repeated[lane] = bits[i_index];
    end
    assign tied[0] = '0;
    assign tied[1] = 1'b1;
    bit_flow_bit_sink u_bit_sink(.i_data(selected));
endmodule
