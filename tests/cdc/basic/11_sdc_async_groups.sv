// Test 11: Two-clock design with SDC-declared async clock groups.
// Exercises the SDC ingestion path of the CDC pipeline:
//   - create_clock for clk_a and clk_b
//   - set_clock_groups -asynchronous {clk_a} {clk_b}
//   - a 2-flop synchronizer between the two domains.
//
// Expected (with --sdc 11_sdc_async_groups.sdc):
//   0 violations, 1 INFO (sync'd async crossing), 1 crossing with
//   relationship="asynchronous".

module sdc_async_groups (
    input  logic clk_a,
    input  logic clk_b,
    input  logic rst_n,
    input  logic data_in,
    output logic data_out
);

    logic q_a;
    logic sync_ff1, sync_ff2;

    always_ff @(posedge clk_a or negedge rst_n) begin
        if (!rst_n)
            q_a <= 1'b0;
        else
            q_a <= data_in;
    end

    always_ff @(posedge clk_b or negedge rst_n) begin
        if (!rst_n) begin
            sync_ff1 <= 1'b0;
            sync_ff2 <= 1'b0;
        end else begin
            sync_ff1 <= q_a;
            sync_ff2 <= sync_ff1;
        end
    end

    assign data_out = sync_ff2;

endmodule
