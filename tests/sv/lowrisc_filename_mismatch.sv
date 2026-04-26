// US-39F single-module FileNameMismatch fixture.
// File basename is `lowrisc_filename_mismatch` but the module is named
// `not_matching_module_name`, so enforce_file_module_match should fire
// exactly once (and prohibit_multiple_modules_per_file should not, because
// the file declares exactly one module).
module not_matching_module_name (
    input  logic clk_i,
    input  logic rst_ni,
    input  logic [7:0] data_i,
    output logic [7:0] data_o
);
    logic [7:0] data_q;
    always_ff @(posedge clk_i or negedge rst_ni) begin
        if (!rst_ni) data_q <= 8'h00;
        else         data_q <= data_i;
    end
    assign data_o = data_q;
endmodule
