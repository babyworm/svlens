// Test 44 (paired NEGATIVE for fixture 43): SDC-less single-domain
// design where parent and submodule both clock from the same port
// (`cb`). After clock unification, NO cross-domain crossing should
// appear -- the submodule's `clk_i` shares the parent's `cb`
// ClockSource, so q_a (cb) -> u_sync.q (cb) -> q_dst (cb) is one
// single domain.
//
// Guards against the unification fix over-creating ClockSources
// (e.g., if a future change relaxes the isResetName filter or
// auto-registers ports that aren't used as clocks).
//
// Expected: 0 violations, 0 cautions, 0 infos, 0 crossings.

module neg_clk_unify_single_domain (
    input  logic cb,
    input  logic rst_n,
    input  logic d
);

    logic q_a, q_dst, sync_out;

    always_ff @(posedge cb or negedge rst_n) begin
        if (!rst_n) q_a <= 1'b0; else q_a <= d;
    end

    sub_clk_44 u_sync (
        .clk_i  (cb),
        .rst_ni (rst_n),
        .d_i    (q_a),
        .q_o    (sync_out)
    );

    always_ff @(posedge cb or negedge rst_n) begin
        if (!rst_n) q_dst <= 1'b0;
        else        q_dst <= sync_out;
    end

endmodule

module sub_clk_44 (
    input  logic clk_i,
    input  logic rst_ni,
    input  logic d_i,
    output logic q_o
);
    logic q;
    always_ff @(posedge clk_i or negedge rst_ni) begin
        if (!rst_ni) q <= 1'b0; else q <= d_i;
    end
    assign q_o = q;
endmodule
