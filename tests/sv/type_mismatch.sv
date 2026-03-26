module signed_producer (output logic signed [15:0] o_coeff);
    assign o_coeff = -16'sd100;
endmodule

module unsigned_consumer (input logic [15:0] i_data);
endmodule

module type_mismatch_top;
    logic signed [15:0] coeff;
    logic [15:0] data;
    signed_producer u_prod (.o_coeff(coeff));
    unsigned_consumer u_cons (.i_data(data));
    assign data = coeff;
endmodule
