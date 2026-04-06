// Packed struct member access test
module packed_struct (
    input  logic [7:0] a,
    input  logic [7:0] b,
    output logic [7:0] y,
    output logic [7:0] z
);

    typedef struct packed {
        logic [7:0] hi;
        logic [7:0] lo;
    } pair_t;

    pair_t p;

    assign p.hi = a;
    assign p.lo = b;
    assign y = p.hi + p.lo;
    assign z = p.hi ^ p.lo;

endmodule
