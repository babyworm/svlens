// Slice/concat test: bus repacking with slicing and concatenation
module slice_concat (
    input  logic [15:0] a,
    input  logic [15:0] b,
    output logic [31:0] y
);

    assign y = {a[15:8], b[7:0], a[7:0], b[15:8]};

endmodule
