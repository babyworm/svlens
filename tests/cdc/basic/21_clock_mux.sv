// Test 21: Glitchless clock mux selecting between clk_a and clk_b at
// runtime. The mux output drives a downstream flop. Real-world clock
// muxes use a small handshake state machine to avoid glitches at the
// switching moment, but for this fixture we model the simple BAD case
// (raw `assign clk_mux = sel ? clk_a : clk_b;`) so a CDC tool has the
// chance to flag it.
//
// Expected (current behavior, to be measured): the tool may either
//   (a) flag clk_mux as a derived clock with both parents and warn
//       about glitch risk, or
//   (b) accept it silently and rely on the human to declare clock
//       relationships in SDC.
//
// This fixture pins the current behavior so that future improvements
// (a dedicated clock-mux glitch rule) can update the golden.

module clock_mux (
    input  logic clk_a,
    input  logic clk_b,
    input  logic sel,
    input  logic rst_n,
    input  logic data_in
);

    // Combinational mux on clocks -- glitch hazard at switch.
    logic clk_mux;
    assign clk_mux = sel ? clk_a : clk_b;

    logic q;
    always_ff @(posedge clk_mux or negedge rst_n) begin
        if (!rst_n)
            q <= 1'b0;
        else
            q <= data_in;
    end

endmodule
