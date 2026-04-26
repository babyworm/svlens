// Codex cross-review: SourceTextScanner reachability fixture.
// Two modules in one file:
//   * `unreachable_top` is a candidate top; clean (no source-text
//     violations).
//   * `unrelated_sibling` is NOT instantiated by `unreachable_top` and
//     contains intentional source-text violations (long line, hard
//     tab, trailing whitespace).
//
// Reachability semantics (Codex Round 2 cross-review):
//   Source-text rules apply at FILE granularity. When this entire file
//   is unrelated to the requested top (e.g. a different fixture's top
//   is selected), nothing here is scanned -- that is the cross-FILE
//   suppression contract verified by the "unrelated file" test.
//
//   But when the requested top IS in this file (or this file contains
//   one of its children), the WHOLE buffer is admitted: the sibling's
//   violations DO appear, because physical-line rules can't be
//   attributed to a single module declaration.  This is the
//   file-level scope contract verified by the "sibling lock-in" test.
//
// MultipleModulesPerFile WILL fire whenever this file is admitted,
// because it contains two module declarations.
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
