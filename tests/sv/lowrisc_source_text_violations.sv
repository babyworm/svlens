// US-39E / US-39F source-text style violation fixture.
// This file intentionally contains:
//   * Lines exceeding 100 characters (LineTooLong)
//   * A line with a hard tab character (HardTab)
//   * A line with trailing whitespace (TrailingWhitespace)
//   * Two module declarations in one file (MultipleModulesPerFile)
//   * Modules whose names do NOT match the file basename (FileNameMismatch)
//
// NOTE: enforce_file_module_match must be enabled in the test YAML to trigger FileNameMismatch.

// ─── Module 1: intentional violations ─────────────────────────────────────────────────────────────
module source_text_primary (
	input  logic clk_i,   // BAD: hard tab indent; also this comment line itself is padded to exceed 100 chars
    input  logic rst_ni,   // trailing whitespace follows:      
    input  logic [7:0] data_i,
    output logic [7:0] data_o
);
    assign data_o = data_i;
endmodule

// ─── Module 2: second module in same file (triggers MultipleModulesPerFile) ──────────────────────
module source_text_secondary (
    input  logic clk_i,
    output logic data_o
);
    assign data_o = clk_i;
endmodule
