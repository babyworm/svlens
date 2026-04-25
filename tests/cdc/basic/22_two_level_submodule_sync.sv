// Test 22: 2-level deep submodule synchronizer chain.
//
// top
// |-- src_path (writes ptr_q on clk_a, exposes ptr_o)
// `-- dst_path (input ptr_i, instantiates sync_2ff that crosses to clk_b)
//
// This mirrors the cdc_fifo_gray_src / cdc_fifo_gray_dst pattern: the
// signal exits module A's output port, becomes a top-level wire, enters
// module B's input port, then enters sync_2ff inside module B. Three
// port crossings between source FF and the destination sync FF.
//
// Expected (after Finding 2 fix): 0 violations, 0 cautions, 1 INFO,
//           1 crossing -- u_src.q -> u_dst.u_sync.ff1, sync_type=two_ff.
// The fix threads the parent_port_chain through processInstanceEdges
// and findFFByName so a port-to-port-to-wire chain across submodule
// boundaries resolves to the source FF.

module two_level_submodule_sync (
    input  logic clk_a,
    input  logic clk_b,
    input  logic rst_n,
    input  logic data_in,
    output logic data_out
);

    logic ptr_a_to_b;

    src_path u_src (
        .clk    (clk_a),
        .rst_n  (rst_n),
        .d      (data_in),
        .q      (ptr_a_to_b)
    );

    dst_path u_dst (
        .clk    (clk_b),
        .rst_n  (rst_n),
        .d      (ptr_a_to_b),
        .q      (data_out)
    );

endmodule

module src_path (
    input  logic clk,
    input  logic rst_n,
    input  logic d,
    output logic q
);

    // Realistic pattern: an internal register drives the output port via
    // a continuous assign. The Finding 2 fix in connectivity.cpp lets
    // the connectivity tracker chase this rename so the parent's
    // wire_map still resolves to the underlying flop.
    logic q_q;

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n)
            q_q <= 1'b0;
        else
            q_q <= d;
    end

    assign q = q_q;

endmodule

module dst_path (
    input  logic clk,
    input  logic rst_n,
    input  logic d,
    output logic q
);

    logic sync_out;

    sync_2ff u_sync (
        .clk_dst_i (clk),
        .rst_ni    (rst_n),
        .d_src_i   (d),
        .q_dst_o   (sync_out)
    );

    assign q = sync_out;

endmodule

module sync_2ff (
    input  logic clk_dst_i,
    input  logic rst_ni,
    input  logic d_src_i,
    output logic q_dst_o
);

    logic ff1, ff2;

    always_ff @(posedge clk_dst_i or negedge rst_ni) begin
        if (!rst_ni) begin
            ff1 <= 1'b0;
            ff2 <= 1'b0;
        end else begin
            ff1 <= d_src_i;
            ff2 <= ff1;
        end
    end

    assign q_dst_o = ff2;

endmodule
