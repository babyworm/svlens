// Test 04: 3-FF synchronizer (high-frequency designs)
// Expected: 0 violations, 1 INFO

module three_ff_sync (
    input  logic clk_a,
    input  logic clk_b,
    input  logic rst_n,
    input  logic data_in
);

    logic q_a;
    logic sync_ff1, sync_ff2, sync_ff3;

    always_ff @(posedge clk_a or negedge rst_n) begin
        if (!rst_n)
            q_a <= 1'b0;
        else
            q_a <= data_in;
    end

    // 3-FF synchronizer
    always_ff @(posedge clk_b or negedge rst_n) begin
        if (!rst_n) begin
            sync_ff1 <= 1'b0;
            sync_ff2 <= 1'b0;
            sync_ff3 <= 1'b0;
        end else begin
            sync_ff1 <= q_a;
            sync_ff2 <= sync_ff1;
            sync_ff3 <= sync_ff2;
        end
    end

endmodule
