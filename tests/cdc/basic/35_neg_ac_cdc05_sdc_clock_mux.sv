// Test 35: Negative pair for Ac_cdc05. The combinational mux output
// `clk_sdc_gen` is declared in the SDC companion file as a
// `create_generated_clock`, so the user has explicitly modelled it as
// a clock that synthesis honours. The Ac_cdc05 detector skips
// generated clocks declared via SDC.
//
// Expected (with --sdc 35_neg_ac_cdc05_sdc_clock_mux.sdc):
//   0 violations, 0 cautions, 0 infos, 0 crossings.

module neg_ac_cdc05_sdc_clock_mux (
    input  logic clk_a,
    input  logic clk_b,
    input  logic sel,
    input  logic rst_n,
    input  logic data_in
);

    logic clk_sdc_gen;
    assign clk_sdc_gen = sel ? clk_a : clk_b;

    logic q;
    always_ff @(posedge clk_sdc_gen or negedge rst_n) begin
        if (!rst_n)
            q <= 1'b0;
        else
            q <= data_in;
    end

endmodule
