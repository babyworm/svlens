// Test 03: Proper 2-FF synchronizer
// Expected: 0 violations, 1 INFO (properly synchronized crossing)

module two_ff_sync (
    input  logic clk_a,
    input  logic clk_b,
    input  logic rst_n,
    input  logic data_in
);

    logic q_a;       // domain A
    logic sync_ff1;  // domain B — first sync stage
    logic sync_ff2;  // domain B — second sync stage

    // FF in clock domain A
    always_ff @(posedge clk_a or negedge rst_n) begin
        if (!rst_n)
            q_a <= 1'b0;
        else
            q_a <= data_in;
    end

    // 2-FF synchronizer in clock domain B
    always_ff @(posedge clk_b or negedge rst_n) begin
        if (!rst_n) begin
            sync_ff1 <= 1'b0;
            sync_ff2 <= 1'b0;
        end else begin
            sync_ff1 <= q_a;      // first sync stage
            sync_ff2 <= sync_ff1; // second sync stage
        end
    end

endmodule
