module nested_struct (
    input  logic [7:0] a,
    output logic [7:0] y
);
    typedef struct packed {
        logic [3:0] hi;
        logic [3:0] lo;
    } nibble_pair_t;

    typedef struct packed {
        nibble_pair_t upper;
        nibble_pair_t lower;
    } quad_t;

    quad_t q;
    assign q = {a[7:4], a[3:0], a[3:0], a[7:4]};
    assign y = q.upper.hi + q.lower.lo;
endmodule
