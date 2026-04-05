// For loop test: always_comb with constant-bound for loop
module for_loop (
    input  logic [31:0] data_in,
    output logic [31:0] data_out
);

    always_comb begin
        for (int i = 0; i < 4; i++) begin
            data_out[i*8 +: 8] = data_in[i*8 +: 8] ^ 8'hFF;
        end
    end

endmodule
