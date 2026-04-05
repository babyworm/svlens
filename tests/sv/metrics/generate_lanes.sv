// Generate-for test: 4 identical lanes via generate block
module generate_lanes (
    input  logic [31:0] data_in,
    input  logic [31:0] mask,
    output logic [31:0] data_out
);

    genvar i;
    generate
        for (i = 0; i < 4; i++) begin : gen_lane
            assign data_out[i*8 +: 8] = data_in[i*8 +: 8] ^ mask[i*8 +: 8];
        end
    endgenerate

endmodule
