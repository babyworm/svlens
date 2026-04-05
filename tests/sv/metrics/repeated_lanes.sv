// Repeated bit-lane test: same XOR operation across byte lanes
module repeated_lanes (
    input  logic [31:0] a,
    input  logic [31:0] b,
    output logic [31:0] y
);

    assign y[7:0]   = a[7:0]   ^ b[7:0];
    assign y[15:8]  = a[15:8]  ^ b[15:8];
    assign y[23:16] = a[23:16] ^ b[23:16];
    assign y[31:24] = a[31:24] ^ b[31:24];

endmodule
