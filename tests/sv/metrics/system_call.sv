module system_call (
    input  logic [7:0] a,
    output logic [7:0] y,
    output logic [3:0] z
);
    assign y = $signed(a) + 8'sd1;
    assign z = 4'($clog2(8));
endmodule
