// Test 06: Proper 2-FF synchronizer where the sync FFs live inside a
// dedicated sub-module (the common "sync" / "sync_wedge" pattern from
// pulp-platform/common_cells and OpenTitan's prim_flop_2sync).
// Expected: 0 violations, 1 INFO (properly synchronized crossing),
//           1 crossing src_clk -> dst_clk via u_sync.
//
// This fixture exists because flat 2-FF synchronizers (see fixture 03)
// are trivially detected, but real designs almost always wrap the sync
// pair in a reusable module. svlens must trace the signal ownership
// across the instance boundary.

module submodule_sync (
    input  logic clk_a,
    input  logic clk_b,
    input  logic rst_n,
    input  logic data_in
);

    logic q_a;       // domain A
    logic sync_out;  // domain B -- output of the sub-module sync

    // FF in clock domain A
    always_ff @(posedge clk_a or negedge rst_n) begin
        if (!rst_n)
            q_a <= 1'b0;
        else
            q_a <= data_in;
    end

    // 2-FF synchronizer wrapped in a sub-module instance.
    sync_2ff u_sync (
        .clk_dst_i (clk_b),
        .rst_ni    (rst_n),
        .d_src_i   (q_a),
        .q_dst_o   (sync_out)
    );

endmodule

// Reusable 2-flop synchronizer primitive.
module sync_2ff (
    input  logic clk_dst_i,
    input  logic rst_ni,
    input  logic d_src_i,
    output logic q_dst_o
);

    logic ff1, ff2;

    always_ff @(posedge clk_dst_i or negedge rst_ni) begin
        if (!rst_ni) begin
            ff1 <= 1'b0;
            ff2 <= 1'b0;
        end else begin
            ff1 <= d_src_i;
            ff2 <= ff1;
        end
    end

    assign q_dst_o = ff2;

endmodule
