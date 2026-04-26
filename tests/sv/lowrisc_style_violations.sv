// Comprehensive fixture exercising the lowRISC style guide examples.
// Each module corresponds to a category from
// https://github.com/lowRISC/style-guides/blob/master/VerilogCodingStyle.md
//
// Modules ending with `_good` use the canonical lowRISC examples and
// are expected to produce ZERO convention violations from the
// matching --convention examples/styles/lowrisc.yaml run.
//
// Modules ending with `_bad` deliberately violate one or more rules
// and are checked by the [lowrisc_style] integration test for
// expected INFO entries.

// ─── Category 1: port direction (GOOD) ────────────────────────────
module ports_good (
    input              clk_i,
    input              rst_ni,
    input        [7:0] d_i,
    output logic [7:0] q_o
);
    assign q_o = d_i;
endmodule

// ─── Category 1: ports + differential pair + inout (GOOD) ─────────
module ports_simple_good (
    input         clk_i,
    input         rst_ni,
    input  [15:0] data_i,
    input         valid_i,
    output        ready_o,
    inout  [7:0]  driver_io,
    output        lvds_po,
    output        lvds_no
);
    assign ready_o = valid_i;
    assign lvds_po = data_i[0];
    assign lvds_no = ~data_i[0];
endmodule

// ─── Category 1: BAD port names ───────────────────────────────────
// Violations:
//   * `RstN` not lowercase
//   * `dataIn` not lowercase, no `_i` suffix
//   * `o_DataValid` mixed case (legacy prefix style is also wrong)
//   * `enable_n` is `_n` suffix on input, not matching reset pattern
//   * `bad_inst` instance prefix missing `u_`
module ports_bad (
    input              clk_i,
    input              RstN,
    input        [7:0] dataIn,
    input              i_command,
    input              enable_n,
    output logic [7:0] o_DataValid,
    output             o_status
);
    inner_used u_inner1 (
        .clk_i (clk_i),
        .data_i(dataIn),
        .data_o(o_DataValid)
    );
    inner_used bad_inst (  // missing u_ prefix
        .clk_i (clk_i),
        .data_i(dataIn),
        .data_o()
    );
    assign o_status = i_command & enable_n;
endmodule

module inner_used (
    input              clk_i,
    input        [7:0] data_i,
    output logic [7:0] data_o
);
    assign data_o = data_i;
endmodule

// ─── Category 2: clock signals (GOOD) ─────────────────────────────
module clocks_good (
    input clk_i,
    input clk_dram_i,
    input rst_ni
);
    logic q;
    always_ff @(posedge clk_i or negedge rst_ni)
        if (!rst_ni) q <= 1'b0; else q <= 1'b1;
endmodule

// ─── Category 3: reset signals (GOOD) ─────────────────────────────
module resets_good (
    input clk_i,
    input rst_ni,
    input rst_domain_ni
);
    logic q;
    always_ff @(posedge clk_i or negedge rst_ni)
        if (!rst_ni) q <= 1'b0; else q <= 1'b1;
endmodule

// ─── Category 6: instance naming (GOOD) ───────────────────────────
module instances_good (
    input        clk_i,
    input        rst_ni,
    input  [7:0] data_i,
    output [7:0] data_o
);
    // lowRISC accepts `i_<name>` and `u_<name>` instance prefixes.
    inner_used u_my_instance (
        .clk_i (clk_i),
        .data_i(data_i),
        .data_o(data_o)
    );
endmodule

// ─── Reject `_<digit>` suffix (BAD) ───────────────────────────────
// `foo_1` violates the digit-tail rule; pipeline should be `foo_q2`.
module digit_suffix_bad (
    input         clk_i,
    input         rst_ni,
    input  [7:0]  foo_1,
    output [7:0]  foo_2
);
    assign foo_2 = foo_1;
endmodule

// ─── Legacy always @(*) (BAD) ─────────────────────────────────────
// lowRISC requires `always_ff` for sequential and `always_comb` for
// combinational. `always @(*)` and `always @(posedge clk)` are
// legacy SystemVerilog forms.
module legacy_always_bad (
    input        clk_i,
    input        rst_ni,
    input  [7:0] data_i,
    output [7:0] data_o,
    output [7:0] reg_o
);
    logic [7:0] reg_q;
    // BAD: legacy combinational always
    logic [7:0] comb_w;
    always @(*) begin
        comb_w = data_i ^ 8'hAA;
    end
    assign data_o = comb_w;
    // BAD: legacy sequential always
    always @(posedge clk_i or negedge rst_ni) begin
        if (!rst_ni) reg_q <= '0;
        else         reg_q <= data_i;
    end
    assign reg_o = reg_q;
endmodule

// ─── Modern always_ff / always_comb (GOOD) ───────────────────────
module modern_always_good (
    input        clk_i,
    input        rst_ni,
    input  [7:0] data_i,
    output [7:0] reg_o
);
    logic [7:0] reg_q;
    always_ff @(posedge clk_i or negedge rst_ni) begin
        if (!rst_ni) reg_q <= '0;
        else         reg_q <= data_i;
    end
    assign reg_o = reg_q;
endmodule

// ─── Parameter / typedef naming (BAD) ─────────────────────────────
// lowRISC: parameters use UpperCamelCase; typedefs end with `_t`
// (struct/union) or `_e` (enum). The fixture below intentionally
// breaks both.
module param_typedef_bad #(
    parameter int unsigned width      = 8,   // BAD: lower_case
    parameter int unsigned MAX_DEPTH  = 32   // BAD: ALL_CAPS at parameter level
) (
    input        clk_i,
    input  [7:0] data_i,
    output [7:0] data_o
);
    typedef logic [7:0] my_byte;             // BAD: missing _t suffix
    typedef enum logic [1:0] { A, B, C } op; // BAD: missing _e suffix

    my_byte byte_q;
    op      op_q;
    always_ff @(posedge clk_i) byte_q <= data_i;
    assign data_o = byte_q;
endmodule

// ─── Parameter / typedef naming (GOOD) ────────────────────────────
module param_typedef_good #(
    parameter  int unsigned Width    = 8,
    localparam int unsigned MaxDepth = 32
) (
    input        clk_i,
    input  [Width-1:0] data_i,
    output [Width-1:0] data_o
);
    typedef logic [Width-1:0] byte_t;
    typedef enum logic [1:0] { OpA, OpB, OpC } opcode_e;

    byte_t   byte_q;
    opcode_e op_q;
    always_ff @(posedge clk_i) byte_q <= data_i;
    assign data_o = byte_q;
endmodule

// ─── Anonymous enum (BAD) ─────────────────────────────────────────
// lowRISC: every enum must be named via typedef (`typedef enum
// {...} <name>_e;`). Inline anonymous enums are prohibited.
module anonymous_enum_bad (
    input        clk_i,
    output       data_o
);
    enum logic [1:0] { Read, Write } req_access;
    assign data_o = (req_access == Read);
endmodule

// ─── Generate block naming (BAD) ──────────────────────────────────
// lowRISC: every generate block must have an explicit `: name`
// label. Slang synthesizes "genblk<N>" when the user omits it.
module unnamed_generate_bad #(
    parameter int unsigned NumLanes = 4
) (
    input  [NumLanes-1:0] data_i,
    output [NumLanes-1:0] data_o
);
    // BAD: generate-for without `: lanes` label
    for (genvar ii = 0; ii < NumLanes; ii++) begin
        assign data_o[ii] = data_i[ii];
    end
endmodule

// ─── Generate block naming (GOOD) ─────────────────────────────────
module named_generate_good #(
    parameter int unsigned NumLanes = 4
) (
    input  [NumLanes-1:0] data_i,
    output [NumLanes-1:0] data_o
);
    for (genvar ii = 0; ii < NumLanes; ii++) begin : my_lanes
        assign data_o[ii] = data_i[ii];
    end
endmodule
