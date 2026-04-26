// Fixture for the lowRISC-style convention checker.
// Intentionally violates several rules so the YAML config can be
// exercised end-to-end:
//   * `clk` is fine (matches clock_pattern)
//   * `RstN` is wrong: not lowercase, not matching reset_pattern
//   * `dataIn` is wrong: not lowercase, no `i_` prefix
//   * `o_DataValid` is wrong: not lowercase
//   * `enable_n` triggers active_low_suffix INFO (not a recognized
//     reset/clock pattern)
//   * `i_command` is correct: matches input_prefix
//   * `o_status` is correct: matches output_prefix

module lowrisc_inner (
    input  logic        clk,
    input  logic        RstN,
    input  logic [7:0]  dataIn,
    input  logic        i_command,
    input  logic        enable_n,
    output logic [7:0]  o_DataValid,
    output logic        o_status
);
    assign o_DataValid = dataIn;
    assign o_status    = i_command & enable_n;
endmodule

module lowrisc_violator (
    input  logic       clk,
    input  logic       rst_n,
    input  logic [7:0] data_in,
    output logic [7:0] data_out
);
    logic       i_command;
    logic       enable_n;
    logic       o_status;
    assign i_command = 1'b0;
    assign enable_n  = rst_n;
    BadInstance bad (
        .clk         (clk),
        .RstN        (rst_n),
        .dataIn      (data_in),
        .i_command   (i_command),
        .enable_n    (enable_n),
        .o_DataValid (data_out),
        .o_status    (o_status)
    );
endmodule

// Note: BadInstance is intentionally lowrisc_inner — kept as a
// distinct typedef name so the instance label `bad` (not `u_bad`)
// also surfaces an instance-prefix violation.
module BadInstance (
    input  logic        clk,
    input  logic        RstN,
    input  logic [7:0]  dataIn,
    input  logic        i_command,
    input  logic        enable_n,
    output logic [7:0]  o_DataValid,
    output logic        o_status
);
    assign o_DataValid = dataIn;
    assign o_status    = i_command & enable_n;
endmodule
