// Test 52 (paired NEGATIVE for fixture 49): same hierarchical
// reference into a digit-medial-underscore generate label
// (`gen_2stage[1].q_inner`), but parent and submodule share the
// same physical clock. After clock unification + the suffix-scan
// fallback for label-name format mismatch, both FFs end up in the
// same domain — there must be NO CDC crossing reported.
//
// Architect Round 11 recommendation: every fixture exercising a
// detection-rule corner should have a paired negative to prove the
// fix doesn't over-classify same-domain reads as crossings.
//
// Expected: 0 violations, 0 cautions, 0 infos, 0 crossings.

module neg_gen_label_digit_medial_same_domain (
    input  logic cb,
    input  logic rst_n,
    input  logic d
);

    logic q_dst;

    sub_52 u_sub (.clk_i(cb), .rst_ni(rst_n), .d_i(d));

    always_ff @(posedge cb or negedge rst_n) begin
        if (!rst_n) q_dst <= 1'b0;
        else        q_dst <= u_sub.gen_2stage[1].q_inner;
    end

endmodule

module sub_52 (
    input  logic clk_i,
    input  logic rst_ni,
    input  logic d_i
);
    for (genvar i = 0; i < 4; i++) begin : gen_2stage
        logic q_inner;
        always_ff @(posedge clk_i or negedge rst_ni) begin
            if (!rst_ni) q_inner <= 1'b0;
            else         q_inner <= d_i;
        end
    end
endmodule
