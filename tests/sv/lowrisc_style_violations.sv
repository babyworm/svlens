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
    logic enable_q;
    always_ff @(posedge clk_i or negedge rst_ni)
        if (!rst_ni) enable_q <= 1'b0; else enable_q <= 1'b1;
endmodule

// ─── Category 3: reset signals (GOOD) ─────────────────────────────
module resets_good (
    input clk_i,
    input rst_ni,
    input rst_domain_ni
);
    logic active_q;
    always_ff @(posedge clk_i or negedge rst_ni)
        if (!rst_ni) active_q <= 1'b0; else active_q <= 1'b1;
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

// ─── Case statement (BAD: missing unique + default) ──────────────
module case_bad (
    input  [1:0] sel_i,
    output       hit_o
);
    logic h;
    always_comb begin
        case (sel_i)
            2'b00: h = 1'b0;
            2'b01: h = 1'b1;
            // BAD: no `unique`, no `default:`
        endcase
    end
    assign hit_o = h;
endmodule

// ─── Case statement (GOOD: unique + default) ─────────────────────
module case_good (
    input  [1:0] sel_i,
    output       hit_o
);
    logic h;
    always_comb begin
        unique case (sel_i)
            2'b00:   h = 1'b0;
            2'b01:   h = 1'b1;
            default: h = 1'b0;
        endcase
    end
    assign hit_o = h;
endmodule

// ─── 2-state types (BAD: lowRISC requires `logic`) ───────────────
module two_state_bad (
    input  logic clk_i,
    output logic data_o
);
    bit [7:0]  buf_q;        // BAD: bit (2-state)
    int        counter_q;    // BAD: int (32-bit 2-state)
    byte       sample_q;     // BAD: byte
    always_ff @(posedge clk_i) begin
        buf_q     <= 8'h00;
        counter_q <= 0;
        sample_q  <= 8'h00;
    end
    assign data_o = buf_q[0];
endmodule

// ─── always_ff registered-output q-suffix (BAD) ──────────────────
// lowRISC: always_ff non-blocking LHS must end with `_q` (or `_q<n>`
// for pipeline stages). Names like `state`/`data`/`reg_value` violate.
module ff_q_suffix_bad (
    input        clk_i,
    input        rst_ni,
    input  [7:0] data_i,
    output [7:0] data_o,
    output       state_o
);
    logic [7:0] data_value;   // BAD: missing _q
    logic       state;        // BAD: missing _q
    always_ff @(posedge clk_i or negedge rst_ni) begin
        if (!rst_ni) begin
            data_value <= '0;
            state      <= 1'b0;
        end else begin
            data_value <= data_i;
            state      <= 1'b1;
        end
    end
    assign data_o  = data_value;
    assign state_o = state;
endmodule

// ─── Reset polarity: comma syntax (BAD) ──────────────────────────
// lowRISC mandates `or` keyword in always_ff sensitivity lists, not comma.
module reset_polarity_bad (
    input        clk_i,
    input        rst_ni,
    input  [7:0] data_i,
    output [7:0] data_o
);
    logic [7:0] data_q;
    always_ff @(posedge clk_i, negedge rst_ni) begin
        if (!rst_ni) data_q <= '0;
        else         data_q <= data_i;
    end
    assign data_o = data_q;
endmodule

// ─── R1 MAJOR: bracket-indexed active-low reset (GOOD) ───────────
// Locks in the fix that strips trailing `[...]` before active-low
// suffix classification.  `rst_n[0]` was previously misclassified
// as active-high because ends_with("_n") was false on the literal
// "rst_n[0]" string returned by toString().
module reset_polarity_bracketed_good (
    input  logic        clk_i,
    input  logic [1:0]  rst_n_arr,
    input  logic [7:0]  data_i,
    output logic [7:0]  data_o
);
    logic [7:0] data_q;
    always_ff @(posedge clk_i or negedge rst_n_arr[0]) begin
        if (!rst_n_arr[0]) data_q <= '0;
        else               data_q <= data_i;
    end
    assign data_o = data_q;
endmodule

// ─── Reset polarity: or syntax (GOOD) ────────────────────────────
// Canonical lowRISC form uses `or` between posedge clk and negedge rst.
module reset_polarity_good (
    input        clk_i,
    input        rst_ni,
    input  [7:0] data_i,
    output [7:0] data_o
);
    logic [7:0] data_q;
    always_ff @(posedge clk_i or negedge rst_ni) begin
        if (!rst_ni) data_q <= '0;
        else         data_q <= data_i;
    end
    assign data_o = data_q;
endmodule

// ─── FF d-suffix: comb input misnamed (BAD) ──────────────────────
// lowRISC requires always_comb to drive `<base>_d` when the
// registered output is `<base>_q`. Here the comb input is called
// `valid_next` instead of `valid_d`.
module ff_d_suffix_bad (
    input        clk_i,
    input        rst_ni,
    input        valid_i,
    output       valid_o
);
    logic valid_q;
    logic valid_next;  // BAD: should be valid_d
    always_comb begin
        valid_next = valid_i;
    end
    always_ff @(posedge clk_i or negedge rst_ni) begin
        if (!rst_ni) valid_q <= 1'b0;
        else         valid_q <= valid_next;
    end
    assign valid_o = valid_q;
endmodule

// ─── FF d-suffix: canonical _d -> _q pairing (GOOD) ─────────────
// Uses canonical `valid_d` -> `valid_q` pairing as required by
// lowRISC style.
module ff_d_suffix_good (
    input        clk_i,
    input        rst_ni,
    input        valid_i,
    output       valid_o
);
    logic valid_q;
    logic valid_d;
    always_comb begin
        valid_d = valid_i;
    end
    always_ff @(posedge clk_i or negedge rst_ni) begin
        if (!rst_ni) valid_q <= 1'b0;
        else         valid_q <= valid_d;
    end
    assign valid_o = valid_q;
endmodule

// ─── US-39C: wildcard port connection (BAD) ───────────────────────
// lowRISC prohibits `.*` because it silently binds ports by name
// match -- an accidental rename can silently disconnect a port.
// `dotstar_child` is the sub-module being instantiated.
module dotstar_child (
    input  logic clk_i,
    input  logic [7:0] data_i,
    output logic [7:0] data_o
);
    assign data_o = data_i;
endmodule

// BAD: uses `.*` -- should emit WildcardPortConnection INFO.
module dotstar_bad (
    input  logic clk_i,
    input  logic [7:0] data_i,
    output logic [7:0] data_o
);
    dotstar_child u_child (.*);
endmodule

// GOOD: explicit named connections -- no WildcardPortConnection INFO.
module dotstar_good (
    input  logic clk_i,
    input  logic [7:0] data_i,
    output logic [7:0] data_o
);
    dotstar_child u_child (
        .clk_i (clk_i),
        .data_i(data_i),
        .data_o(data_o)
    );
endmodule

// ─── US-39D: bare integer literal (BAD) ───────────────────────────
// lowRISC requires explicit width specifiers on numeric literals in
// RTL assignments.  Bare `2`, `255` etc. are prohibited; use
// `8'd2`, `8'hff`, or the unsized forms `'0`/`'1`.
module width_lit_bad (
    input  logic clk_i,
    input  logic rst_ni,
    output logic [7:0] data_o
);
    logic [7:0] count_q;
    // BAD: bare literal `2` on RHS of continuous assign
    assign data_o = count_q + 2;
    always_ff @(posedge clk_i or negedge rst_ni)
        if (!rst_ni) count_q <= 0;
        else         count_q <= count_q + 1;
endmodule

// GOOD: explicit-width literals everywhere.
module width_lit_good (
    input  logic clk_i,
    input  logic rst_ni,
    output logic [7:0] data_o
);
    logic [7:0] count_q;
    assign data_o = count_q + 8'd2;
    always_ff @(posedge clk_i or negedge rst_ni)
        if (!rst_ni) count_q <= '0;
        else         count_q <= count_q + 8'd1;
endmodule

// ─── R1 MAJOR: end-anchored _q suffix (GOOD) ─────────────────────
// Locks in the fix that anchors `_q` to end-of-name (or `_q[0-9]+$`).
// Both `data_qual_next` and `data_q_next` are combinational always_ff
// targets that should NOT be flagged: the previous rfind("_q") logic
// matched `_q` mid-string and incorrectly emitted MissingQSuffix.
// Note: these names are still NB-LHS in always_ff so they DO get the
// MissingQSuffix violation -- that part is correct.  The regression
// test below asserts only that the BAD-style `_q_next` and `_qual_next`
// patterns DO get flagged (because they are NOT end-anchored `_q`),
// while pipeline stages `_q2`/`_q3` DO NOT get flagged.
module q_suffix_anchor_check (
    input        clk_i,
    input        rst_ni,
    input  [7:0] data_i,
    output [7:0] data_o
);
    logic [7:0] data_qual_next;  // BAD: _q is mid-string, not anchored
    logic [7:0] data_q_next;     // BAD: _q not at end (followed by _next)
    logic [7:0] data_q;          // GOOD: ends with _q
    logic [7:0] data_q2;         // GOOD: ends with _q[0-9]+
    always_ff @(posedge clk_i or negedge rst_ni) begin
        if (!rst_ni) begin
            data_qual_next <= '0;
            data_q_next    <= '0;
            data_q         <= '0;
            data_q2        <= '0;
        end else begin
            data_qual_next <= data_i;
            data_q_next    <= data_qual_next;
            data_q         <= data_q_next;
            data_q2        <= data_q;
        end
    end
    assign data_o = data_q2;
endmodule

// ─── always_ff with proper _q (GOOD) ─────────────────────────────
// data_q (single stage), valid_q / valid_q2 / valid_q3 (pipeline)
module ff_q_suffix_good (
    input        clk_i,
    input        rst_ni,
    input  [7:0] data_i,
    input        valid_i,
    output [7:0] data_o,
    output       valid_q3_o
);
    logic [7:0] data_q;
    logic       valid_q, valid_q2, valid_q3;
    always_ff @(posedge clk_i or negedge rst_ni) begin
        if (!rst_ni) begin
            data_q   <= '0;
            valid_q  <= 1'b0;
            valid_q2 <= 1'b0;
            valid_q3 <= 1'b0;
        end else begin
            data_q   <= data_i;
            valid_q  <= valid_i;
            valid_q2 <= valid_q;
            valid_q3 <= valid_q2;
        end
    end
    assign data_o     = data_q;
    assign valid_q3_o = valid_q3;
endmodule
