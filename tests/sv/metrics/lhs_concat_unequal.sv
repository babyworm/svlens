module lhs_concat_unequal (
    input  logic [15:0] data,
    output logic [7:0]  hi,
    output logic [3:0]  mid,
    output logic [3:0]  lo
);
    assign {hi, mid, lo} = data;
endmodule
