// Hierarchy test: top module instantiating a submodule with internal logic
module sub_adder (
    input  logic [7:0] a,
    input  logic [7:0] b,
    output logic [7:0] sum
);
    assign sum = a + b;
endmodule

module hierarchy_cone (
    input  logic [7:0] x,
    input  logic [7:0] y,
    output logic [7:0] result
);
    logic [7:0] added;
    sub_adder u_add (.a(x), .b(y), .sum(added));
    assign result = added ^ 8'hFF;
endmodule
