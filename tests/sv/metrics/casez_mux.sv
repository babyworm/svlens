// Casez test: always_comb with casez wildcard patterns
module casez_mux (
    input  logic [3:0] opcode,
    input  logic [7:0] a,
    input  logic [7:0] b,
    output logic [7:0] y
);

    always_comb begin
        casez (opcode)
            4'b1???: y = a + b;
            4'b01??: y = a - b;
            4'b001?: y = a & b;
            default: y = 8'h0;
        endcase
    end

endmodule
