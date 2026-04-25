// Test 37: LHS-concatenation 2-FF sync chain. Mirrors the ZipCPU
// afifo.v idiom that compresses a 2-FF sync stage into one always
// statement using LHS / RHS concatenations:
//
//   { wr_rgray, rgray_cross } <= { rgray_cross, rgray };
//
// Semantically: rgray_cross gets rgray (first stage), wr_rgray gets
// the previous rgray_cross (second stage) -- a textbook 2-FF chain.
//
// Expected (after fix): 0 violations, 0 cautions, 1 INFO crossing
// (rgray -> rgray_cross via 2-FF concat sync), 1 crossing.
//
// Pre-fix svlens behaviour (codified gap): 0 crossings -- the
// connectivity tracker walks AssignmentExpression LHS as
// NamedValueExpression only, so a Concatenation LHS is silently
// dropped from FFEdge construction.

module concat_lhs_sync_chain (
    input  wire       wclk,
    input  wire       rclk,
    input  wire       rst_n,
    input  wire [3:0] r_increment
);

    reg [3:0] rgray;
    reg [3:0] rgray_cross, wr_rgray;

    always @(posedge rclk or negedge rst_n) begin
        if (!rst_n)
            rgray <= 4'h0;
        else
            rgray <= rgray ^ r_increment;
    end

    always @(posedge wclk or negedge rst_n) begin
        if (!rst_n)
            { wr_rgray, rgray_cross } <= 8'h00;
        else
            { wr_rgray, rgray_cross } <= { rgray_cross, rgray };
    end

endmodule
