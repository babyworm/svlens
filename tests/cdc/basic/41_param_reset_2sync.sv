// Test 41 (positive): OpenTitan-style 2-FF synchronizer where the
// reset value is a `parameter`. Before the parameter-fanin fix,
// `q_o <= ResetValue` inflated each FF's fanin to {ResetValue, d_i}
// (size 2), which silently disabled findNextFF / detectSyncPattern
// for every prim_flop_2sync-derived synchronizer in OpenTitan,
// Ariane, and any RTL that parameterizes the reset value.
//
// After the fix in collectReferencedSignals (filtering
// SymbolKind::Parameter / TypeParameter / EnumValue), the chain
// rgray_q -> u_sync.u_sync_1.q_o is recognised as a 2-FF
// synchronizer (sync_type=two_ff). With the SDC sidecar declaring
// the clocks asynchronous, the disposition is INFO.
//
// Expected: 0 violations, 0 cautions, 1 info, 1 crossing.

module param_reset_2sync (
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

    pf2sync_param #(.ResetValue(1'b0)) u_sync (
        .clk_i  (dst_clk),
        .rst_ni (rst_n),
        .d_i    (rgray_q)
    );

endmodule

module pf2sync_param #(
    parameter logic ResetValue = 1'b0
) (
    input  logic clk_i,
    input  logic rst_ni,
    input  logic d_i
);
    logic d_o, intq;
    always_comb d_o = d_i;
    pflop_param #(.ResetValue(ResetValue))
        u_sync_1 (.clk_i, .rst_ni, .d_i(d_o), .q_o(intq));
    pflop_param #(.ResetValue(ResetValue))
        u_sync_2 (.clk_i, .rst_ni, .d_i(intq), .q_o());
endmodule

module pflop_param #(
    parameter logic ResetValue = 1'b0
) (
    input  logic clk_i,
    input  logic rst_ni,
    input  logic d_i,
    output logic q_o
);
    always_ff @(posedge clk_i or negedge rst_ni) begin
        if (!rst_ni) q_o <= ResetValue;  // PARAMETER reset, not literal
        else         q_o <= d_i;
    end
endmodule
