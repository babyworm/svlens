// Test 08: Asynchronous FIFO pointer CDC.
// Write and read pointers are gray-encoded before crossing clock domains
// via 2-flop synchronizers. This mirrors the classic Cummings async-FIFO
// pattern (SNUG 2002).
//
// Expected: 0 violations, 2 INFOs (wr_ptr_gray -> rd_clk, rd_ptr_gray ->
// wr_clk), 2 crossings.

module gray_fifo_ptr #(
    parameter int unsigned DEPTH_LOG2 = 3
) (
    input  logic wr_clk,
    input  logic rd_clk,
    input  logic rst_n,
    input  logic wr_en,
    input  logic rd_en
);

    localparam int unsigned PW = DEPTH_LOG2 + 1;

    // ---- write domain ----
    logic [PW-1:0] wr_ptr_bin_q, wr_ptr_bin_next;
    logic [PW-1:0] wr_ptr_gray_q, wr_ptr_gray_next;

    assign wr_ptr_bin_next  = wr_ptr_bin_q + {{(PW-1){1'b0}}, wr_en};
    assign wr_ptr_gray_next = (wr_ptr_bin_next >> 1) ^ wr_ptr_bin_next;

    always_ff @(posedge wr_clk or negedge rst_n) begin
        if (!rst_n) begin
            wr_ptr_bin_q  <= '0;
            wr_ptr_gray_q <= '0;
        end else begin
            wr_ptr_bin_q  <= wr_ptr_bin_next;
            wr_ptr_gray_q <= wr_ptr_gray_next;
        end
    end

    // ---- read domain ----
    logic [PW-1:0] rd_ptr_bin_q, rd_ptr_bin_next;
    logic [PW-1:0] rd_ptr_gray_q, rd_ptr_gray_next;

    assign rd_ptr_bin_next  = rd_ptr_bin_q + {{(PW-1){1'b0}}, rd_en};
    assign rd_ptr_gray_next = (rd_ptr_bin_next >> 1) ^ rd_ptr_bin_next;

    always_ff @(posedge rd_clk or negedge rst_n) begin
        if (!rst_n) begin
            rd_ptr_bin_q  <= '0;
            rd_ptr_gray_q <= '0;
        end else begin
            rd_ptr_bin_q  <= rd_ptr_bin_next;
            rd_ptr_gray_q <= rd_ptr_gray_next;
        end
    end

    // ---- wr_ptr_gray crossing into read domain (2-flop sync) ----
    logic [PW-1:0] wr_ptr_sync_ff1, wr_ptr_sync_ff2;

    always_ff @(posedge rd_clk or negedge rst_n) begin
        if (!rst_n) begin
            wr_ptr_sync_ff1 <= '0;
            wr_ptr_sync_ff2 <= '0;
        end else begin
            wr_ptr_sync_ff1 <= wr_ptr_gray_q;
            wr_ptr_sync_ff2 <= wr_ptr_sync_ff1;
        end
    end

    // ---- rd_ptr_gray crossing into write domain (2-flop sync) ----
    logic [PW-1:0] rd_ptr_sync_ff1, rd_ptr_sync_ff2;

    always_ff @(posedge wr_clk or negedge rst_n) begin
        if (!rst_n) begin
            rd_ptr_sync_ff1 <= '0;
            rd_ptr_sync_ff2 <= '0;
        end else begin
            rd_ptr_sync_ff1 <= rd_ptr_gray_q;
            rd_ptr_sync_ff2 <= rd_ptr_sync_ff1;
        end
    end

endmodule
