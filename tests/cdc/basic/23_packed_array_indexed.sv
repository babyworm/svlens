// Test 23: Multi-bit packed-array signal crossing clock domains via
// per-bit synchronizers in a generate-for loop. This mirrors the gray
// pointer crossing in pulp-platform/common_cells/src/cdc_fifo_gray.sv:
//
//   output logic [PTR_W-1:0] async_wptr_o;
//   for (genvar i = 0; i < PTR_W; i++) begin : gen_sync
//     sync u_sync (.serial_i(async_wptr[i]), .serial_o(synced[i]));
//   end
//
// Expected: 0 violations, 4 cautions (rule Ac_cdc04 -- per-bit 2-FF
//           sync of a wide bus without gray code or single-bit
//           handshake; bits resolve metastability at different cycles
//           and the receiver may see intermediate values), 0 infos,
//           4 crossings.
//
// The connectivity tracker walks the generate-for and produces one
// crossing per bit; the wide-bus rule then flags each because the
// source register is multi-bit and not gray-coded.

module packed_array_indexed (
    input  logic       src_clk,
    input  logic       dst_clk,
    input  logic       rst_n,
    input  logic [3:0] data_in,
    output logic [3:0] data_out
);

    logic [3:0] wptr_q;
    logic [3:0] synced;

    // Source flops on src_clk: 4-bit packed register.
    always_ff @(posedge src_clk or negedge rst_n) begin
        if (!rst_n)
            wptr_q <= 4'h0;
        else
            wptr_q <= data_in;
    end

    // Per-bit 2-FF synchronizer in a generate-for loop, matching the
    // common_cells cdc_fifo_gray idiom.
    for (genvar i = 0; i < 4; i++) begin : gen_sync
        logic ff1, ff2;
        always_ff @(posedge dst_clk or negedge rst_n) begin
            if (!rst_n) begin
                ff1 <= 1'b0;
                ff2 <= 1'b0;
            end else begin
                ff1 <= wptr_q[i];
                ff2 <= ff1;
            end
        end
        assign synced[i] = ff2;
    end

    assign data_out = synced;

endmodule
