// Test 43 (positive): SDC-less clock domain unification through
// submodule port mapping. Parent has unconventional port names
// `ca` / `cb` that DO NOT match the isClockName heuristic
// (`*clk*`/`*clock*`/`*ck*`). Without the unification fix the
// submodule's `clk_i` port becomes a fresh ClockSource via
// FFClassifier path-3, splitting the destination FF into a
// phantom domain and producing TWO crossings (the genuine
// q_a -> u_sync.q AND a spurious u_sync.q -> q_dst).
//
// After the fix in propagateInstance + isPortUsedAsClock, `cb` is
// auto-registered as a ClockSource on demand and `clk_i` shares
// its identity. With unification, u_sync.q and q_dst are both in
// the cb domain, so the chain q_a -> u_sync.q -> q_dst forms a
// proper 2-FF sync (sync_type=two_ff). Without unification, the
// disposition would be 2 spurious crossings (q_a -> u_sync.q AND
// u_sync.q -> q_dst).
//
// Expected: 0 violations, 0 cautions, 1 info, 1 crossing.

module clk_unify_no_sdc (
    input  logic ca,
    input  logic cb,
    input  logic rst_n,
    input  logic d
);

    logic q_a, q_dst, sync_out;

    always_ff @(posedge ca or negedge rst_n) begin
        if (!rst_n) q_a <= 1'b0; else q_a <= d;
    end

    sub_clk_43 u_sync (
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

module sub_clk_43 (
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
