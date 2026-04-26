// Codex cross-review: SourceTextScanner reachability fixture.
// Two modules in one file:
//   * `unreachable_top` is the requested top; clean (no source-text
//     violations).
//   * `unrelated_sibling` is NOT instantiated by the top, and contains
//     intentional source-text violations (long line, hard tab,
//     trailing whitespace).
// When the SourceTextScanner is correctly scoped to topModule
// reachability, the sibling's violations must be suppressed.
//
// MultipleModulesPerFile WILL fire because the file contains two
// module declarations and `unreachable_top` is reachable; that is
// expected and asserted by the test.
module unreachable_top (
    input  logic clk_i,
    output logic data_o
);
    assign data_o = clk_i;
endmodule

module unrelated_sibling (
	input  logic clk_i,   // BAD: tab indent + this comment is intentionally padded out way past the configured eighty column limit so LineTooLong fires here
    input  logic rst_ni,   // BAD: trailing whitespace next:      
    output logic data_o
);
    assign data_o = clk_i & rst_ni;
endmodule
