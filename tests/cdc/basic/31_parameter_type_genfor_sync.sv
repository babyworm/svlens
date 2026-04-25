// Test 31: cdc_fifo_gray-style parameter-type packed signal crossing
// through generate-for + module instance sync. Mirrors the
// `parameter type T = logic` idiom used by pulp-platform/common_cells.
// Pins the cdc_fifo_gray claim ("8 VIOLATIONS detected end-to-end")
// against silent regressions.
//
// Expected: 0 violations, 4 cautions (one per bit, all CAUTION via
//           Ac_cdc01-style fan-out concern across the same domain pair),
//           0 infos, 4 crossings.

module parameter_type_genfor_sync #(
    parameter type T = logic [3:0]
) (
    input  logic src_clk,
    input  logic dst_clk,
    input  logic rst_n,
    input  T     data_in,
    output T     data_out
);

    T data_src;

    always_ff @(posedge src_clk or negedge rst_n) begin
        if (!rst_n)
            data_src <= '0;
        else
            data_src <= data_in;
    end

    sync_wrapper #(.T(T)) u_wrap (
        .clk_i  (dst_clk),
        .rst_ni (rst_n),
        .d_i    (data_src),
        .q_o    (data_out)
    );

endmodule

module sync_wrapper #(
    parameter type T = logic
) (
    input  logic clk_i,
    input  logic rst_ni,
    input  T     d_i,
    output T     q_o
);

    localparam int W = $bits(T);

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
