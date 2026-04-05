// Simple cone test: output driven by continuous assigns with basic operations
module simple_cone (
    input  logic        clk,
    input  logic        rst_n,
    input  logic [7:0]  a,
    input  logic [7:0]  b,
    input  logic        sel,
    output logic [7:0]  y,
    output logic [7:0]  z
);

    logic [7:0] sum;
    logic [7:0] diff;

    assign sum  = a + b;
    assign diff = a - b;
    assign y = sel ? sum : diff;
    assign z = a & b;

endmodule
