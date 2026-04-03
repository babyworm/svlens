// Test 01: Single clock domain, no crossings expected
// Expected: 0 violations, 0 cautions

module single_domain (
    input  logic       clk,
    input  logic       rst_n,
    input  logic [7:0] data_in,
    output logic [7:0] data_out
);

    logic [7:0] stage1, stage2;

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            stage1 <= 8'h0;
            stage2 <= 8'h0;
        end else begin
            stage1 <= data_in;
            stage2 <= stage1;
        end
    end

    assign data_out = stage2;

endmodule
