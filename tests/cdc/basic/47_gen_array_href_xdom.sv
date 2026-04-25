// Test 47 (positive): hierarchical reference INTO a generate-
// array entry across clock-domain boundary. Locks the Round 10
// fix in connectivity.cpp::findFFByName that handles the slang
// `gen_blk[N]` (getHierarchicalPath syntax) vs `genblkN`
// (getExternalName form) format mismatch.
//
// Pattern: parent module reads a specific genvar entry's
// internal FF via `u_sub.gen_blk[1].q_inner` and that FF lives
// in a different clock domain. The crossing must be detected.
//
// Expected: 1 violation, 0 cautions, 0 infos, 1 crossing.

module gen_array_href_xdom (
    input  logic ca,
    input  logic cb,
    input  logic rst_n,
    input  logic d
);

    logic q_dst;

    sub_47 u_sub (.clk_i(ca), .rst_ni(rst_n), .d_i(d));

    always_ff @(posedge cb or negedge rst_n) begin
        if (!rst_n) q_dst <= 1'b0;
        else        q_dst <= u_sub.gen_blk[1].q_inner;
    end

endmodule

module sub_47 (
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
