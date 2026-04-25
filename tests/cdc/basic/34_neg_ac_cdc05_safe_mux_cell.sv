// Test 34: Negative pair for Ac_cdc05. The clock is selected by an
// instance of `my_clkmux_cell`, a user-defined glitch-free clock mux
// primitive. The runner registers `my_clkmux_cell` via
// --glitch-free-mux-cell on the svlens command line so the Ac_cdc05
// detector treats the cell's output as a safe clock and does NOT
// raise a violation.
//
// Expected: 0 violations, 0 cautions, 0 infos, 0 crossings.

module neg_ac_cdc05_safe_mux_cell (
    input  logic clk_a,
    input  logic clk_b,
    input  logic sel,
    input  logic rst_n,
    input  logic data_in
);

    logic clk_safe;

    // Instance of a user-registered glitch-free clock mux primitive.
    // The CDC tool is told (via --glitch-free-mux-cell my_clkmux_cell)
    // that this cell guarantees glitch-free behaviour at the switch.
    my_clkmux_cell u_mux (
        .clk_a (clk_a),
        .clk_b (clk_b),
        .sel   (sel),
        .clk_o (clk_safe)
    );

    logic q;
    always_ff @(posedge clk_safe or negedge rst_n) begin
        if (!rst_n)
            q <= 1'b0;
        else
            q <= data_in;
    end

endmodule

// Stub primitive whose internal mechanics svlens does not need to
// understand -- the user has registered it as glitch-free.
module my_clkmux_cell (
    input  logic clk_a,
    input  logic clk_b,
    input  logic sel,
    output logic clk_o
);
    assign clk_o = sel ? clk_a : clk_b;
endmodule
