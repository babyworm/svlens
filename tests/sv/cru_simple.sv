module cru_inner(input logic a, output logic b);
    assign b = a;
endmodule

module cru_top;
    logic w;
    logic z;
    cru_inner u_i(.a(w), .b(z));
endmodule
