typedef struct packed {
    logic [1:0] rst_sys_n;
} candidate_reset_bundle_t;

module prim_clock_mux2(input logic clk0_i, clk1_i, sel_i, output logic clk_o);
    assign clk_o = sel_i ? clk1_i : clk0_i;
endmodule

module candidate_reset_bit_ff(input logic clk_i, ext_rst_n, output logic q_o);
    always_ff @(posedge clk_i or negedge ext_rst_n)
        if (!ext_rst_n) q_o <= 1'b0;
        else q_o <= 1'b1;
endmodule

module candidate_reset_source(input logic clk_i, ext_rst_n, scan_rst_ni, scanmode_i,
                              output candidate_reset_bundle_t resets_o);
    logic [1:0] rst_pair;
    candidate_reset_bit_ff u_bit(.clk_i(clk_i), .ext_rst_n(ext_rst_n), .q_o(rst_pair[0]));
    assign rst_pair[1] = 1'b0;

    prim_clock_mux2 u_mux(.clk0_i(rst_pair[0]), .clk1_i(scan_rst_ni),
                          .sel_i(scanmode_i), .clk_o(resets_o.rst_sys_n[0]));
    assign resets_o.rst_sys_n[1] = 1'b0;
endmodule

module candidate_computed_reset_source(input logic clk_i, ext_rst_n, scan_rst_ni, scanmode_i,
                                       output candidate_reset_bundle_t resets_o);
    logic rst_sync_n;
    always_ff @(posedge clk_i or negedge ext_rst_n)
        if (!ext_rst_n) rst_sync_n <= 1'b0;
        else rst_sync_n <= 1'b1;

    prim_clock_mux2 u_mux(.clk0_i(~rst_sync_n), .clk1_i(scan_rst_ni),
                          .sel_i(scanmode_i), .clk_o(resets_o.rst_sys_n[0]));
    assign resets_o.rst_sys_n[1] = 1'b0;
endmodule

module candidate_fixed_inverted_reset_source(input logic clk_i, ext_rst_n, scan_rst_ni,
                                             output candidate_reset_bundle_t resets_o);
    logic rst_sync_n;
    always_ff @(posedge clk_i or negedge ext_rst_n)
        if (!ext_rst_n) rst_sync_n <= 1'b0;
        else rst_sync_n <= 1'b1;

    prim_clock_mux2 u_mux(.clk0_i(!rst_sync_n), .clk1_i(scan_rst_ni),
                          .sel_i(1'b0), .clk_o(resets_o.rst_sys_n[0]));
    assign resets_o.rst_sys_n[1] = 1'b0;
endmodule

module candidate_wide_inverted_reset_source(input logic clk_i, ext_rst_n, scan_rst_ni, scanmode_i,
                                            output candidate_reset_bundle_t resets_o);
    logic rst_sync_n;
    logic [1:0] rst_pair;
    always_ff @(posedge clk_i or negedge ext_rst_n)
        if (!ext_rst_n) rst_sync_n <= 1'b0;
        else rst_sync_n <= 1'b1;

    assign rst_pair = {1'b0, rst_sync_n};
    prim_clock_mux2 u_mux(.clk0_i(~rst_pair), .clk1_i(scan_rst_ni),
                          .sel_i(scanmode_i), .clk_o(resets_o.rst_sys_n[0]));
    assign resets_o.rst_sys_n[1] = 1'b0;
endmodule

module candidate_cast_reset_source(input logic clk_i, ext_rst_n, scan_rst_ni, scanmode_i,
                                   output candidate_reset_bundle_t resets_o);
    logic rst_sync_n;
    logic [1:0] rst_pair;
    always_ff @(posedge clk_i or negedge ext_rst_n)
        if (!ext_rst_n) rst_sync_n <= 1'b0;
        else rst_sync_n <= 1'b1;

    assign rst_pair = {1'b0, rst_sync_n};
    prim_clock_mux2 u_mux(.clk0_i(1'(rst_pair)), .clk1_i(scan_rst_ni),
                          .sel_i(scanmode_i), .clk_o(resets_o.rst_sys_n[0]));
    assign resets_o.rst_sys_n[1] = 1'b0;
endmodule

module candidate_static_reset_source #(parameter logic SelectScan = 1'b0)
                                     (input logic clk_i, ext_rst_n, scan_rst_ni,
                                      output candidate_reset_bundle_t resets_o);
    logic rst_sync_n;
    always_ff @(posedge clk_i or negedge ext_rst_n)
        if (!ext_rst_n) rst_sync_n <= 1'b0;
        else rst_sync_n <= 1'b1;

    prim_clock_mux2 u_mux(.clk0_i(rst_sync_n), .clk1_i(scan_rst_ni),
                          .sel_i(SelectScan), .clk_o(resets_o.rst_sys_n[0]));
    assign resets_o.rst_sys_n[1] = 1'b0;
endmodule

module candidate_reset_sink(input logic clk_i, rst_ni, d_i, output logic q_o);
    always_ff @(posedge clk_i or negedge rst_ni)
        if (!rst_ni) q_o <= 1'b0;
        else q_o <= d_i;
endmodule

module conditional_reset_mux_top(input logic clk_a, clk_b, ext_rst_n, scan_rst_ni,
                                 scanmode_i, d_i, output logic q_o);
    candidate_reset_bundle_t resets, other_resets;
    candidate_reset_source u_source(.clk_i(clk_a), .ext_rst_n(ext_rst_n),
                                    .scan_rst_ni(scan_rst_ni), .scanmode_i(scanmode_i),
                                    .resets_o(resets));
    candidate_reset_source u_other(.clk_i(clk_a), .ext_rst_n(ext_rst_n),
                                   .scan_rst_ni(scan_rst_ni), .scanmode_i(scanmode_i),
                                   .resets_o(other_resets));
    candidate_reset_sink u_sink(.clk_i(clk_b), .rst_ni(resets.rst_sys_n[0]),
                                .d_i(d_i), .q_o(q_o));
endmodule

module conditional_reset_mux_computed_top(input logic clk_a, clk_b, ext_rst_n, scan_rst_ni,
                                          scanmode_i, d_i, output logic q_o);
    candidate_reset_bundle_t resets;
    candidate_computed_reset_source u_source(.clk_i(clk_a), .ext_rst_n(ext_rst_n),
                                             .scan_rst_ni(scan_rst_ni), .scanmode_i(scanmode_i),
                                             .resets_o(resets));
    candidate_reset_sink u_sink(.clk_i(clk_b), .rst_ni(resets.rst_sys_n[0]),
                                .d_i(d_i), .q_o(q_o));
endmodule

module conditional_reset_mux_inverted_static_top(input logic clk_a, clk_b, ext_rst_n,
                                                 scan_rst_ni, d_i, output logic q_o);
    candidate_reset_bundle_t resets;
    candidate_fixed_inverted_reset_source u_source(.clk_i(clk_a), .ext_rst_n(ext_rst_n),
                                                   .scan_rst_ni(scan_rst_ni), .resets_o(resets));
    candidate_reset_sink u_sink(.clk_i(clk_b), .rst_ni(resets.rst_sys_n[0]),
                                .d_i(d_i), .q_o(q_o));
endmodule

module conditional_reset_mux_wide_inversion_top(input logic clk_a, clk_b, ext_rst_n,
                                                scan_rst_ni, scanmode_i, d_i, output logic q_o);
    candidate_reset_bundle_t resets;
    candidate_wide_inverted_reset_source u_source(.clk_i(clk_a), .ext_rst_n(ext_rst_n),
                                                  .scan_rst_ni(scan_rst_ni), .scanmode_i(scanmode_i),
                                                  .resets_o(resets));
    candidate_reset_sink u_sink(.clk_i(clk_b), .rst_ni(resets.rst_sys_n[0]),
                                .d_i(d_i), .q_o(q_o));
endmodule

module conditional_reset_mux_cast_top(input logic clk_a, clk_b, ext_rst_n, scan_rst_ni,
                                      scanmode_i, d_i, output logic q_o);
    candidate_reset_bundle_t resets;
    candidate_cast_reset_source u_source(.clk_i(clk_a), .ext_rst_n(ext_rst_n),
                                         .scan_rst_ni(scan_rst_ni), .scanmode_i(scanmode_i),
                                         .resets_o(resets));
    candidate_reset_sink u_sink(.clk_i(clk_b), .rst_ni(resets.rst_sys_n[0]),
                                .d_i(d_i), .q_o(q_o));
endmodule

module conditional_reset_mux_static0_top(input logic clk_a, clk_b, ext_rst_n,
                                         scan_rst_ni, d_i, output logic q_o);
    candidate_reset_bundle_t resets;
    candidate_static_reset_source u_source(.clk_i(clk_a), .ext_rst_n(ext_rst_n),
                                           .scan_rst_ni(scan_rst_ni), .resets_o(resets));
    candidate_reset_sink u_sink(.clk_i(clk_b), .rst_ni(resets.rst_sys_n[0]),
                                .d_i(d_i), .q_o(q_o));
endmodule

module conditional_reset_mux_static1_top(input logic clk_a, clk_b, ext_rst_n,
                                         scan_rst_ni, d_i, output logic q_o);
    candidate_reset_bundle_t resets;
    candidate_static_reset_source #(.SelectScan(1'b1)) u_source
        (.clk_i(clk_a), .ext_rst_n(ext_rst_n),
         .scan_rst_ni(scan_rst_ni), .resets_o(resets));
    candidate_reset_sink u_sink(.clk_i(clk_b), .rst_ni(resets.rst_sys_n[0]),
                                .d_i(d_i), .q_o(q_o));
endmodule

module conditional_reset_mux_staticx_top(input logic clk_a, clk_b, ext_rst_n,
                                         scan_rst_ni, d_i, output logic q_o);
    candidate_reset_bundle_t resets;
    candidate_static_reset_source #(.SelectScan(1'bx)) u_source
        (.clk_i(clk_a), .ext_rst_n(ext_rst_n),
         .scan_rst_ni(scan_rst_ni), .resets_o(resets));
    candidate_reset_sink u_sink(.clk_i(clk_b), .rst_ni(resets.rst_sys_n[0]),
                                .d_i(d_i), .q_o(q_o));
endmodule
