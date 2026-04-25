// Test 26: Closer reproduction of the cdc_fifo_gray clock-inheritance
// pattern: top -> wrapper -> generate-for -> sync_cell. The sync_cell
// has a port named `clk_i`. Inside the wrapper, the gen_sync block
// connects `.clk_i(local_clk_port)` -- the wrapper's own clock port
// named identically to the sync's port.
//
// This is the EXACT layering used by pulp-platform/common_cells/src/
// cdc_fifo_gray_src and cdc_fifo_gray_dst with their per-bit `i_sync`.
//
// Expected (post-fix): the multi-bit register `data_src` is recognised
// as the source, each bit's sync_cell flops are placed in the dst_clk
// domain (NOT the literal clk_i), and the tool reports >=4 cross-clock
// crossings (one per bit).

module genfor_in_wrapper_clock_inherit (
    input  logic       src_clk,
    input  logic       dst_clk,
    input  logic       rst_n,
    input  logic [3:0] data_in
);

    logic [3:0] data_src;

    always_ff @(posedge src_clk or negedge rst_n) begin
        if (!rst_n)
            data_src <= 4'h0;
        else
            data_src <= data_in;
    end

    src_wrapper u_wrap (
        .clk_i  (dst_clk),
        .rst_ni (rst_n),
        .d_i    (data_src),
        .q_o    ()
    );

endmodule

module src_wrapper #(
    parameter int W = 4
) (
    input  logic         clk_i,
    input  logic         rst_ni,
    input  logic [W-1:0] d_i,
    output logic [W-1:0] q_o
);

    for (genvar i = 0; i < W; i++) begin : gen_sync
        sync_cell u_sync (
            .clk_i  (clk_i),
            .rst_ni (rst_ni),
            .d_i    (d_i[i]),
            .q_o    (q_o[i])
        );
    end

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
