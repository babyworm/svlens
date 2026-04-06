// Function call test: always_comb with function calls
module function_call (
    input  logic [7:0] a,
    input  logic [7:0] b,
    input  logic       sel,
    output logic [7:0] y,
    output logic [7:0] z
);

    function automatic logic [7:0] add_sat(input logic [7:0] x, input logic [7:0] y_in);
        logic [8:0] tmp;
        tmp = {1'b0, x} + {1'b0, y_in};
        return tmp[8] ? 8'hFF : tmp[7:0];
    endfunction

    function automatic logic [7:0] mux2(input logic [7:0] d0, input logic [7:0] d1, input logic s);
        return s ? d1 : d0;
    endfunction

    // Continuous assign with function call
    assign y = add_sat(a, b);

    // always_comb with function call
    always_comb begin
        z = mux2(a, b, sel);
    end

endmodule
