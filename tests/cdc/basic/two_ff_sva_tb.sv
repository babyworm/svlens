module two_ff_sva_tb;
    logic clk_a = 1'b0;
    logic clk_b = 1'b0;
    logic rst_n = 1'b0;
    logic data_in = 1'b0;

    two_ff_sync two_ff_sync(.clk_a, .clk_b, .rst_n, .data_in);
    svlens_cdc_assertions checks();

    always #5 clk_a = ~clk_a;
    always #7 clk_b = ~clk_b;

    initial begin
        #20 rst_n = 1'b1;
        repeat (20) begin
            @(negedge clk_a);
            data_in = ~data_in;
        end
        #100 $finish;
    end
endmodule
