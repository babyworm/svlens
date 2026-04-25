// Test 49 (positive): hierarchical reference into a generate-array
// labeled with a digit-medial-underscore name (`gen_2stage`).
// Slang's `getHierarchicalPath()` returns `gen_2stage[1]` for the
// reference, while `getExternalName()` flattens the labeled entry
// to `genblk1` (the auto-name form, since slang may not preserve
// labels with digits in mid-position).
//
// Round 11 fix in connectivity.cpp::findFFByName adds a generic
// "parent-prefix + index + leaf-suffix" scan that bridges this
// pathological label-name mismatch by suffix-matching against
// output_map.
//
// Expected: 1 violation, 0 cautions, 0 infos, 1 crossing.

module gen_label_digit_medial (
    input  logic ca,
    input  logic cb,
    input  logic rst_n,
    input  logic d
);

    logic q_dst;

    sub_49 u_sub (.clk_i(ca), .rst_ni(rst_n), .d_i(d));

    always_ff @(posedge cb or negedge rst_n) begin
        if (!rst_n) q_dst <= 1'b0;
        else        q_dst <= u_sub.gen_2stage[1].q_inner;
    end

endmodule

module sub_49 (
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
