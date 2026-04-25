// Test 45 (positive): parent module reads a sub-module's internal
// FF via hierarchical access (`u_sub.q`) across clock-domain
// boundary. Round 8/9 fix in collectReferencedSignals
// (HierarchicalValue full-path capture) plus findFFByName direct
// hierarchical lookup must classify this as a CDC crossing.
//
// Pattern is common in OSS RTL for debug probes and assertion
// hookups; missing the crossing is a real false negative.
//
// Expected: 1 violation, 0 cautions, 0 infos, 1 crossing.

module hier_ref_xdom (
    input  logic ca,
    input  logic cb,
    input  logic rst_n,
    input  logic d
);

    logic q_dst;

    sub_45 u_sub (.clk_i(ca), .rst_ni(rst_n), .d_i(d));

    always_ff @(posedge cb or negedge rst_n) begin
        if (!rst_n) q_dst <= 1'b0;
        else        q_dst <= u_sub.q;
    end

endmodule

module sub_45 (
    input  logic clk_i,
    input  logic rst_ni,
    input  logic d_i
);
    logic q;
    always_ff @(posedge clk_i or negedge rst_ni) begin
        if (!rst_ni) q <= 1'b0;
        else         q <= d_i;
    end
endmodule
