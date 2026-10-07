module generated_source(input logic [7:0] data_i, output logic [7:0] data_o);
    assign data_o = data_i;
endmodule

module generated_sink(input logic [7:0] data_i);
endmodule

module procedural_generate_mux_top(input logic [7:0] a_i, b_i,
                                   input logic select_i);
    logic [7:0] a, b, selected, tapped;
    generated_source u_a(.data_i(a_i), .data_o(a));
    generated_source u_b(.data_i(b_i), .data_o(b));
    if (1) begin : g_glue
        always_comb selected = select_i ? a : b;
        assign tapped = a;
    end
    generated_sink u_out(.data_i(selected));
    generated_sink u_tap(.data_i(tapped));
endmodule
