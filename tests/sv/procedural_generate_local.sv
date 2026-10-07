module local_source(input logic [7:0] data_i, output logic [7:0] data_o);
    assign data_o = data_i;
endmodule

module local_sink(input logic [7:0] data_i);
endmodule

module procedural_generate_local_top(input logic [7:0] a_i, b_i);
    logic [7:0] a, b;
    local_source u_a(.data_i(a_i), .data_o(a));
    local_source u_b(.data_i(b_i), .data_o(b));
    for (genvar i = 0; i < 2; i++) begin : g_lane
        logic [7:0] local_data;
        if (i == 0) begin : g_a
            always_comb local_data = a;
        end else begin : g_b
            always_comb local_data = b;
        end
        local_sink u_sink(.data_i(local_data));
    end
endmodule
