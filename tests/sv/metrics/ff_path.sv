// FF path test: simple pipeline with combinational logic between FFs
module ff_path (
    input  logic        clk,
    input  logic        rst_n,
    input  logic [7:0]  data_in,
    output logic [7:0]  data_out
);

    logic [7:0] stage1_q;
    logic [7:0] stage2_d;

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n)
            stage1_q <= 8'h0;
        else
            stage1_q <= data_in;
    end

    assign stage2_d = stage1_q + 8'h1;

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n)
            data_out <= 8'h0;
        else
            data_out <= stage2_d;
    end

endmodule
