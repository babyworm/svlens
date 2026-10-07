module inline_width_producer(
    // svlens: waive WIDTH_MISMATCH path=inline_width_top.u_prod.o_data reason: intentional extension
    output logic [7:0] o_data
);
    assign o_data = 8'h5a;
endmodule

module inline_width_consumer(input logic [15:0] i_data);
endmodule

module inline_width_top;
    logic [7:0] data;
    inline_width_producer u_prod(.o_data(data));
    inline_width_consumer u_cons(.i_data(data));
endmodule
