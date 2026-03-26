module producer (output logic [31:0] o_data);
    assign o_data = 32'hDEADBEEF;
endmodule

module consumer (input logic [15:0] i_data);
endmodule

module width_mismatch_top;
    logic [31:0] wide_data;
    logic [15:0] narrow_data;
    producer u_prod (.o_data(wide_data));
    consumer u_cons (.i_data(narrow_data));
    assign narrow_data = wide_data[15:0];
endmodule
