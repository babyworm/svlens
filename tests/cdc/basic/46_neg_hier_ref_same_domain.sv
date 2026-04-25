// Test 46 (paired NEGATIVE for fixture 45): parent reads
// u_sub.q via hierarchical access but both FFs share the same
// physical clock (cb). With clock unification through port
// binding, both end up in the cb domain and there must be NO
// crossing reported.
//
// Guards against the hierarchical-reference fix over-classifying
// same-domain reads as CDC crossings.
//
// Expected: 0 violations, 0 cautions, 0 infos, 0 crossings.

module neg_hier_ref_same_domain (
    input  logic cb,
    input  logic rst_n,
    input  logic d
);

    logic q_dst;

    sub_46 u_sub (.clk_i(cb), .rst_ni(rst_n), .d_i(d));

    always_ff @(posedge cb or negedge rst_n) begin
        if (!rst_n) q_dst <= 1'b0;
        else        q_dst <= u_sub.q;
    end

endmodule

module sub_46 (
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
