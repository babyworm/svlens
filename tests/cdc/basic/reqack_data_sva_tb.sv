module reqack_data_sva_tb;
    logic clk_a = 1'b0;
    logic clk_b = 1'b0;
    logic rst_a_n = 1'b0;
    logic rst_b_n = 1'b0;
    logic req_i = 1'b0;
    logic ack_i = 1'b0;
    logic [3:0] data_ab = '0;
    logic [3:0] out_ab;

    reqack_data_forward_sva_top reqack_data_forward_sva_top(.*);
    svlens_cdc_assertions checks();

    always #5 clk_a = ~clk_a;
    always #7 clk_b = ~clk_b;

    initial begin
        #20;
        rst_a_n = 1'b1;
        rst_b_n = 1'b1;
        #11 data_ab = 4'h3; // legal change while no request is pending
        #10 req_i = 1'b1;
`ifdef SVLENS_REQACK_SVA_MUTATE
        #10 data_ab = 4'h7; // illegal change before ACK reaches the source
`else
        #10;
`endif
        #80 $finish;
    end
endmodule
