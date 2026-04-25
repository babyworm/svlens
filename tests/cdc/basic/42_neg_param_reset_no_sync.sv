// Test 42 (paired NEGATIVE for fixture 41): same parameter-reset
// pattern but with a SINGLE sub-flop instance (no 2nd sync stage).
// The disposition must remain a VIOLATION even after the
// parameter-fanin filter -- the filter must NOT over-classify
// genuine missing-sync conditions.
//
// Expected: 1 violation, 0 cautions, 0 infos, 1 crossing.

module neg_param_reset_no_sync (
    input  logic src_clk,
    input  logic dst_clk,
    input  logic rst_n,
    input  logic data_in
);

    logic rgray_q;
    always_ff @(posedge src_clk or negedge rst_n) begin
        if (!rst_n) rgray_q <= 1'b0;
        else        rgray_q <= data_in;
    end

    // Only ONE sub-flop -- missing the 2nd sync stage.
    pf1_param #(.ResetValue(1'b0)) u_only (
        .clk_i  (dst_clk),
        .rst_ni (rst_n),
        .d_i    (rgray_q)
    );

endmodule

module pf1_param #(
    parameter logic ResetValue = 1'b0
) (
    input  logic clk_i,
    input  logic rst_ni,
    input  logic d_i
);
    logic q;
    always_ff @(posedge clk_i or negedge rst_ni) begin
        if (!rst_ni) q <= ResetValue;
        else         q <= d_i;
    end
endmodule
