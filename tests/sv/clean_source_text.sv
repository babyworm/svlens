// US-39E / US-39F clean reference fixture.
// This file must produce zero source-text style observations when scanned
// with max_line_length=100, prohibit_hard_tabs=true, and
// prohibit_trailing_whitespace=true.  All lines are under 100 chars,
// no tabs, no trailing whitespace, and exactly one module per file.
module clean_source_text (
    input  logic        clk_i,
    input  logic        rst_ni,
    input  logic [7:0]  data_i,
    output logic [7:0]  data_o
);
    logic [7:0] data_q;
    always_ff @(posedge clk_i or negedge rst_ni) begin
        if (!rst_ni) data_q <= 8'h00;
        else         data_q <= data_i;
    end
    assign data_o = data_q;
endmodule
