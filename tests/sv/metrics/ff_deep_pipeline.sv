// Deep pipeline test: 3-stage FF pipeline with combinational logic between stages
module ff_deep_pipeline (
    input  logic       clk,
    input  logic       rst_n,
    input  logic [7:0] data_in,
    output logic [7:0] data_out
);

    logic [7:0] s1_q, s2_q;
    logic [7:0] s1_d, s2_d, s3_d;

    assign s1_d = data_in + 8'h1;
    assign s2_d = s1_q ^ 8'hAA;
    assign s3_d = s2_q & 8'hF0;

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            s1_q <= 8'h0;
            s2_q <= 8'h0;
            data_out <= 8'h0;
        end else begin
            s1_q <= s1_d;
            s2_q <= s2_d;
            data_out <= s3_d;
        end
    end

endmodule
