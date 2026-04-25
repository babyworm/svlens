// Test 48 (paired NEGATIVE for fixture 47): same `u_sub.gen_blk[1].q_inner`
// hierarchical read pattern but parent and submodule share the
// same physical clock (cb). After clock unification + the
// generate-array name normalization, both FFs live in the same
// domain and there must be NO crossing reported.
//
// Guards against the gen-array hier-ref fix over-classifying
// same-domain reads as CDC crossings.
//
// Expected: 0 violations, 0 cautions, 0 infos, 0 crossings.

module neg_gen_array_same_domain (
    input  logic cb,
    input  logic rst_n,
    input  logic d
);

    logic q_dst;

    sub_48 u_sub (.clk_i(cb), .rst_ni(rst_n), .d_i(d));

    always_ff @(posedge cb or negedge rst_n) begin
        if (!rst_n) q_dst <= 1'b0;
        else        q_dst <= u_sub.gen_blk[1].q_inner;
    end

endmodule

module sub_48 (
    input  logic clk_i,
    input  logic rst_ni,
    input  logic d_i
);
    for (genvar i = 0; i < 4; i++) begin : gen_blk
        logic q_inner;
        always_ff @(posedge clk_i or negedge rst_ni) begin
            if (!rst_ni) q_inner <= 1'b0;
            else         q_inner <= d_i;
        end
    end
endmodule
