// Alias-only test: pure passthrough wiring, no logic
module alias_only (
    input  logic [7:0] data_in,
    input  logic       valid_in,
    output logic [7:0] data_out,
    output logic       valid_out
);

    assign data_out  = data_in;
    assign valid_out = valid_in;

endmodule
