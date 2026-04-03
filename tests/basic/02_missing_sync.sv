// Test 02: Direct FF-to-FF cross-domain transfer — no synchronizer
// Expected: 1 VIOLATION (async crossing without sync)

module missing_sync (
    input  logic clk_a,
    input  logic clk_b,
    input  logic rst_n,
    input  logic data_in
);

    logic q_a;  // domain A
    logic q_b;  // domain B

    // FF in clock domain A
    always_ff @(posedge clk_a or negedge rst_n) begin
        if (!rst_n)
            q_a <= 1'b0;
        else
            q_a <= data_in;
    end

    // FF in clock domain B — directly samples q_a (VIOLATION!)
    always_ff @(posedge clk_b or negedge rst_n) begin
        if (!rst_n)
            q_b <= 1'b0;
        else
            q_b <= q_a;  // CDC violation: no synchronizer
    end

endmodule
