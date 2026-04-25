// Test 25: Three-level hierarchy where the deepest sync module's clock
// port is named `clk_i`, but is wired down through a wrapper module
// from the top's dst_clk. This is the cdc_fifo_gray pattern at
// minimum depth: top -> sync_wrapper -> sync_cell.
//
// Before Finding 4 fix: svlens classifies sync_cell's flops as the
// literal `clk_i` domain, hiding the real src_clk -> dst_clk crossing.
//
// After Finding 4 fix: clock-domain inheritance walks the port chain
// and the sync_cell flops are placed in the dst_clk domain. The
// crossing src_clk -> dst_clk is reported as one INFO (proper 2-FF
// sync).
//
// Expected (post-fix): 0 violations, 0 cautions, 1 INFO,
//                      1 crossing src_clk -> dst_clk.

module nested_sync_clock_inherit (
    input  logic src_clk,
    input  logic dst_clk,
    input  logic rst_n,
    input  logic data_in
);

    logic data_src;

    always_ff @(posedge src_clk or negedge rst_n) begin
        if (!rst_n)
            data_src <= 1'b0;
        else
            data_src <= data_in;
    end

    sync_wrapper u_wrap (
        .clk_i  (dst_clk),
        .rst_ni (rst_n),
        .d_i    (data_src),
        .q_o    ()
    );

endmodule

module sync_wrapper (
    input  logic clk_i,
    input  logic rst_ni,
    input  logic d_i,
    output logic q_o
);

    sync_cell u_sync (
        .clk_i  (clk_i),
        .rst_ni (rst_ni),
        .d_i    (d_i),
        .q_o    (q_o)
    );

endmodule

module sync_cell (
    input  logic clk_i,
    input  logic rst_ni,
    input  logic d_i,
    output logic q_o
);

    logic ff1, ff2;

    always_ff @(posedge clk_i or negedge rst_ni) begin
        if (!rst_ni) begin
            ff1 <= 1'b0;
            ff2 <= 1'b0;
        end else begin
            ff1 <= d_i;
            ff2 <= ff1;
        end
    end

    assign q_o = ff2;

endmodule
