// Test 50 (positive): realistic mini-design combining MULTIPLE
// CDC patterns that the Round 7-11 work resolved. The design is a
// shrunken async FIFO pointer crossing that simultaneously
// exercises:
//
// (a) Concat-LHS 2-FF sync chain (Round 7 / ZipCPU afifo idiom):
//     `{wq2_rgray, wq1_rgray} <= {wq1_rgray, rgray_q};`
//
// (b) Parameter-driven reset value (Round 8 / OpenTitan
//     prim_flop): `q <= ResetValue;`
//
// (c) Hierarchical reference into a generate-array entry (Round
//     10 / 11): the parent reads `u_sync.gen_bit[0].q_inner` to
//     observe a per-bit synchronizer output.
//
// (d) Clock unification through non-clock-named parent ports
//     (Round 9): parent uses `wclk` / `rclk` (matches isClockName
//     via `clk` substring) and submodule's `clk_i` ports get
//     unified via the lazy port-driven mechanism.
//
// All four patterns work together in one design. The expected
// output is exactly TWO recognised cross-domain crossings: one
// for the rgray pointer (rclk -> wclk) detected as `two_ff` via
// the concat-LHS sync chain, and one hierarchical-ref readback
// observation that's same-domain in cb (no crossing).
//
// Expected: 0 violations, 0 cautions, 1 info, 1 crossing.

module multi_pattern_async_fifo (
    input  logic       wclk,
    input  logic       rclk,
    input  logic       rst_n,
    input  logic [3:0] r_increment
);

    // Read-side gray pointer (in rclk domain).
    logic [3:0] rgray_q;
    always_ff @(posedge rclk or negedge rst_n) begin
        if (!rst_n) rgray_q <= 4'h0;
        else        rgray_q <= rgray_q ^ r_increment;
    end

    // Concat-LHS 2-FF sync chain in wclk domain (pattern a).
    logic [3:0] wq1_rgray, wq2_rgray;
    always_ff @(posedge wclk or negedge rst_n) begin
        if (!rst_n) begin
            wq2_rgray <= 4'h0;
            wq1_rgray <= 4'h0;
        end else begin
            { wq2_rgray, wq1_rgray } <= { wq1_rgray, rgray_q };
        end
    end

    // Per-bit observation register, in wclk domain, fed via
    // hierarchical reference INTO a generate-array submodule
    // entry (patterns c + d).
    logic obs_q;
    bit_observer u_obs (.clk_i(wclk), .rst_ni(rst_n),
                        .d_i(wq2_rgray[0]));
    always_ff @(posedge wclk or negedge rst_n) begin
        if (!rst_n) obs_q <= 1'b0;
        else        obs_q <= u_obs.gen_bit[0].q_inner;
    end

endmodule

// Submodule with parameter-driven reset (pattern b) and a
// generate-array of single-FF observation taps (pattern c).
module bit_observer #(
    parameter logic ResetValue = 1'b0,
    parameter int   N          = 2
) (
    input  logic clk_i,
    input  logic rst_ni,
    input  logic d_i
);
    for (genvar i = 0; i < N; i++) begin : gen_bit
        logic q_inner;
        always_ff @(posedge clk_i or negedge rst_ni) begin
            if (!rst_ni) q_inner <= ResetValue;  // parameter reset
            else         q_inner <= d_i;
        end
    end
endmodule
