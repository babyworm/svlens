module block_with_debug (
    output logic [7:0] o_debug,
    output logic       o_valid
);
    assign o_debug = 8'hFF;
    assign o_valid = 1'b1;
endmodule

module dangling_top;
    logic valid;
    block_with_debug u_block (.o_debug(), .o_valid(valid));
endmodule
