module producer(
    output logic [7:0] o_data
);
    always_comb begin
        o_data = 8'h3C;
    end
endmodule

module consumer(
    input logic [7:0] i_data
);
endmodule

module procedural_glue_top(input logic clk);
    logic [7:0] prod_data;
    logic [7:0] glue_data;

    producer u_prod(.o_data(prod_data));

    always_comb begin
        glue_data = prod_data;
    end

    consumer u_cons(.i_data(glue_data));
endmodule
