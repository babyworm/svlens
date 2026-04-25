// Test 32: Negative pair for Ac_cdc04 (wide-bus CDC without gray /
// handshake). Mirror of fixture 15 but with a single-bit data path.
// The Ac_cdc04 rule must NOT fire because the source register is
// 1-bit wide; per-bit skew is impossible with a single bit.
//
// Expected: 0 violations, 0 cautions, 1 INFO, 1 crossing.

module neg_ac_cdc04_single_bit (
    input  logic clk_a,
    input  logic clk_b,
    input  logic rst_n,
    input  logic data_in
);

    logic data_a;
    logic sync_ff1, sync_ff2;

    always_ff @(posedge clk_a or negedge rst_n) begin
        if (!rst_n)
            data_a <= 1'b0;
        else
            data_a <= data_in;
    end

    always_ff @(posedge clk_b or negedge rst_n) begin
        if (!rst_n) begin
            sync_ff1 <= 1'b0;
            sync_ff2 <= 1'b0;
        end else begin
            sync_ff1 <= data_a;
            sync_ff2 <= sync_ff1;
        end
    end

endmodule
