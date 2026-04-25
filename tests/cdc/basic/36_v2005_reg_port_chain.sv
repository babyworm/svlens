// Test 36: Verilog-2005 reg-driven port chain (ZipCPU/afifo style).
// The pattern is `output reg [N:0] foo;` declared on the module port
// and driven directly by an always block inside the same module --
// no `assign port = internal_reg;` rename, no SystemVerilog `output
// logic` + `assign`. This is the dominant style in older Verilog-2005
// IP (ZipCPU, OpenCores, academic IP).
//
// Gray-coded read pointer crosses from rclk domain into wclk domain
// via a 2-FF synchronizer. Both domains use direct reg-on-port
// declarations.
//
// Expected (after fix): 0 violations, 0 cautions, 1 INFO (rgray ->
//                       wq2_rgray via 2-FF sync), 1 crossing.
//
// Pre-fix svlens behaviour: 0 crossings -- the connectivity tracker
// chases `assign port = ff` and `always_comb wire = port` but does
// not follow `output reg port; always @(posedge clk) port <= ...;`
// because the port itself is the FF.

module v2005_reg_port_chain (
    input  wire       wclk,
    input  wire       rclk,
    input  wire       rst_n,
    input  wire [3:0] r_increment,
    output reg  [3:0] rgray,
    output reg  [3:0] wq1_rgray,
    output reg  [3:0] wq2_rgray
);

    // rclk-domain: gray pointer driven directly into the output port.
    always @(posedge rclk or negedge rst_n) begin
        if (!rst_n)
            rgray <= 4'h0;
        else
            rgray <= rgray ^ r_increment;
    end

    // wclk-domain: 2-FF synchronizer for the rclk-domain rgray.
    // Both stages are also `output reg` ports.
    always @(posedge wclk or negedge rst_n) begin
        if (!rst_n) begin
            wq1_rgray <= 4'h0;
            wq2_rgray <= 4'h0;
        end else begin
            wq1_rgray <= rgray;
            wq2_rgray <= wq1_rgray;
        end
    end

endmodule
