// Test 10: Signal named foo_div2 that is purely DATA (never used on any
// flop's clock port). The old naming heuristic in clock_tree.cpp
// unconditionally promoted any "q <= ~q" toggle register to a "<lhs>_div2"
// generated clock, which then tripped Ac_cdc09 "clock as data" cautions.
// After the Finding 1 fix, such registers must remain data-only.
//
// Expected: 0 violations, 0 cautions, 0 crossings, exactly one clock
// domain (clk).

module naming_no_false_clock (
    input  logic clk,
    input  logic rst_n,
    input  logic en,
    output logic data_out
);

    // Deliberately misleading name; this is a toggle data register that
    // is NEVER used as a clock.
    logic foo_div2;

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n)
            foo_div2 <= 1'b0;
        else if (en)
            foo_div2 <= ~foo_div2;
    end

    assign data_out = foo_div2;

endmodule
