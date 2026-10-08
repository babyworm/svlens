module gate_cost(
    input logic [7:0] a,
    input logic [7:0] b,
    output logic [7:0] y_wire,
    output logic [7:0] y_and,
    output logic [7:0] y_add,
    output logic [7:0] y_mul
);
    assign y_wire = a;
    assign y_and = a & b;
    assign y_add = a + b;
    assign y_mul = a * b;
endmodule
