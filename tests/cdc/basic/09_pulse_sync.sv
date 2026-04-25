// Test 09: Pulse synchronizer built on toggle + 2-flop sync + XOR.
// A single-cycle pulse in src_clk domain is turned into a toggle,
// synchronized into dst_clk domain, and XOR'd with a delayed copy of
// itself to recover a single-cycle pulse on dst_clk.
//
// This is the canonical pulse_synchronizer primitive
// (Cummings SNUG 2008, pulp-platform/common_cells::pulp_sync_wedge).
//
// Expected: 0 violations, 1 INFO (toggle_src -> dst_clk 2FF sync),
//           1 crossing.

module pulse_sync (
    input  logic src_clk,
    input  logic dst_clk,
    input  logic rst_n,
    input  logic pulse_src_i,   // single-cycle pulse in src_clk domain
    output logic pulse_dst_o    // reconstructed pulse in dst_clk domain
);

    // ---- source-side toggle ----
    logic toggle_src_q;

    always_ff @(posedge src_clk or negedge rst_n) begin
        if (!rst_n)
            toggle_src_q <= 1'b0;
        else if (pulse_src_i)
            toggle_src_q <= ~toggle_src_q;
    end

    // ---- destination-side 2-flop synchronizer ----
    logic sync_ff1, sync_ff2, sync_ff3;

    always_ff @(posedge dst_clk or negedge rst_n) begin
        if (!rst_n) begin
            sync_ff1 <= 1'b0;
            sync_ff2 <= 1'b0;
            sync_ff3 <= 1'b0;
        end else begin
            sync_ff1 <= toggle_src_q;
            sync_ff2 <= sync_ff1;
            sync_ff3 <= sync_ff2;  // delay stage for XOR edge detection
        end
    end

    // Recovered pulse = edge on synchronized toggle.
    assign pulse_dst_o = sync_ff2 ^ sync_ff3;

endmodule
