module cross_link_producer(input logic clk_a, d_i, output logic q_o);
    always_ff @(posedge clk_a) q_o <= d_i;
endmodule

module cross_link_consumer(input logic clk_b, input logic [1:0] i_data,
                           output logic q_o);
    always_ff @(posedge clk_b) q_o <= i_data[0];
endmodule

module cross_mode_link_top(input logic clk_a, clk_b, d_i,
                           output logic q_o);
    logic source_data;
    cross_link_producer u_prod(.clk_a(clk_a), .d_i(d_i), .q_o(source_data));
    cross_link_consumer u_cons(.clk_b(clk_b), .i_data(source_data), .q_o(q_o));
endmodule
