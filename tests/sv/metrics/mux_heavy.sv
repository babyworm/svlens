// Mux-heavy test: nested ternary chain producing deep mux tree
module mux_heavy (
    input  logic [2:0] sel,
    input  logic [7:0] a, b, c, d, e, f, g, h,
    output logic [7:0] y
);

    assign y = sel[2] ? (sel[1] ? (sel[0] ? h : g)
                                : (sel[0] ? f : e))
                      : (sel[1] ? (sel[0] ? d : c)
                                : (sel[0] ? b : a));

endmodule
