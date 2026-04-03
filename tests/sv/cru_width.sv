module cru_wide_src(output logic [31:0] o_data);
    assign o_data = 32'hDEADBEEF;
endmodule

module cru_narrow_snk(input logic [15:0] i_data);
endmodule

module cru_wide_top;
    logic [31:0] bus;
    cru_wide_src u_src(.o_data(bus));
    cru_narrow_snk u_snk(.i_data(bus));
endmodule
