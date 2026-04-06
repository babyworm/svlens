module nested_function_call (
    input  logic [7:0] a,
    input  logic [7:0] b,
    output logic [7:0] y
);
    function automatic logic [7:0] double_it(input logic [7:0] x);
        return x + x;
    endfunction

    function automatic logic [7:0] quad_it(input logic [7:0] x);
        return double_it(double_it(x));
    endfunction

    assign y = quad_it(a) + b;
endmodule
