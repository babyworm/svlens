module debug_source (
    output logic [7:0] o_debug_a,
    output logic [7:0] o_debug_b,
    output logic [7:0] o_data
);
    assign o_debug_a = 8'h01;
    assign o_debug_b = 8'h02;
    assign o_data = 8'hAA;
endmodule

module waiver_top;
    logic [7:0] data;
    debug_source u_dbg (
        .o_debug_a(),
        .o_debug_b(),
        .o_data(data)
    );
endmodule
