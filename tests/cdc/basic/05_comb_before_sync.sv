// Test 05: Combinational logic before sync FF — glitch risk
// Expected: 1 CAUTION (synchronizer present but with glitch path)

module comb_before_sync (
    input  logic clk_a,
    input  logic clk_b,
    input  logic rst_n,
    input  logic data_in,
    input  logic enable
);

    logic q_a;
    logic glitch_wire;
    logic sync_ff1, sync_ff2;

    // FF in domain A
    always_ff @(posedge clk_a or negedge rst_n) begin
        if (!rst_n)
            q_a <= 1'b0;
        else
            q_a <= data_in;
    end

    // Combinational logic BEFORE synchronizer — glitch risk!
    assign glitch_wire = q_a & enable;

    // 2-FF sync in domain B, but fed by combinational output
    always_ff @(posedge clk_b or negedge rst_n) begin
        if (!rst_n) begin
            sync_ff1 <= 1'b0;
            sync_ff2 <= 1'b0;
        end else begin
            sync_ff1 <= glitch_wire;  // CAUTION: comb logic before sync
            sync_ff2 <= sync_ff1;
        end
    end

endmodule
