typedef struct packed {
    logic       valid;
    logic [3:0] hi;
    logic [3:0] lo;
} payload_t;

module producer (
    output logic       o_valid,
    output logic [3:0] o_hi,
    output logic [3:0] o_lo
);
    assign o_valid = 1'b1;
    assign o_hi = 4'hA;
    assign o_lo = 4'h5;
endmodule

module consumer (
    input logic       i_valid,
    input logic [7:0] i_bus
);
endmodule

module member_concat_top;
    payload_t payload;

    producer u_prod (
        .o_valid(payload.valid),
        .o_hi(payload.hi),
        .o_lo(payload.lo)
    );

    consumer u_cons (
        .i_valid(payload.valid),
        .i_bus({payload.hi, payload.lo})
    );
endmodule
