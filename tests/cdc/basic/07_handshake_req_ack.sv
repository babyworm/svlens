// Test 07: 4-phase req/ack handshake with inline 2-flop synchronizers for
// both directions. This is the textbook async handshake -- req crosses
// src_clk -> dst_clk and ack crosses dst_clk -> src_clk, both via a
// dedicated 2-flop sync pair.
//
// Expected: 0 violations, 2 INFOs (both directions properly synced),
//           2 crossings.

module handshake_req_ack (
    input  logic src_clk,
    input  logic dst_clk,
    input  logic rst_n,
    input  logic req_src_in,
    input  logic ack_dst_in
);

    // Source-side state: req toggle on src_clk.
    logic req_src_q;

    always_ff @(posedge src_clk or negedge rst_n) begin
        if (!rst_n)
            req_src_q <= 1'b0;
        else
            req_src_q <= req_src_in;
    end

    // Destination-side 2-flop sync for req_src_q.
    logic req_sync_ff1, req_sync_ff2;

    always_ff @(posedge dst_clk or negedge rst_n) begin
        if (!rst_n) begin
            req_sync_ff1 <= 1'b0;
            req_sync_ff2 <= 1'b0;
        end else begin
            req_sync_ff1 <= req_src_q;
            req_sync_ff2 <= req_sync_ff1;
        end
    end

    // Destination-side state: ack toggle on dst_clk.
    logic ack_dst_q;

    always_ff @(posedge dst_clk or negedge rst_n) begin
        if (!rst_n)
            ack_dst_q <= 1'b0;
        else
            ack_dst_q <= ack_dst_in ^ req_sync_ff2;
    end

    // Source-side 2-flop sync for ack_dst_q.
    logic ack_sync_ff1, ack_sync_ff2;

    always_ff @(posedge src_clk or negedge rst_n) begin
        if (!rst_n) begin
            ack_sync_ff1 <= 1'b0;
            ack_sync_ff2 <= 1'b0;
        end else begin
            ack_sync_ff1 <= ack_dst_q;
            ack_sync_ff2 <= ack_sync_ff1;
        end
    end

endmodule
