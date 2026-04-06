// LHS concatenation decomposition test
module lhs_concat (
    input  logic [7:0] a,
    input  logic [7:0] b,
    output logic [7:0] y,
    output logic [7:0] z
);

    // LHS concat: each element should get its own driver
    assign {y, z} = {a + b, a ^ b};

endmodule
