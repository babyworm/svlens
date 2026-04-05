// Unsupported construct test: contains always_latch which is not handled in MVP
module unsupported_construct (
    input  logic       en,
    input  logic [7:0] d,
    output logic [7:0] q,
    output logic [7:0] y
);

    // Supported: continuous assign
    assign y = d + 8'h1;

    // Unsupported: always_latch (not always_comb or always_ff)
    always_latch begin
        if (en)
            q = d;
    end

endmodule
