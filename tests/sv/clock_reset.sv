// tests/sv/clock_reset.sv — test design for ClockResetAnalyzer
module core (
    input  logic sys_clk,
    input  logic sys_rst_n,
    input  logic [31:0] i_data,
    output logic [31:0] o_data
);
    assign o_data = i_data;
endmodule

module periph (
    input  logic peri_clk,
    input  logic peri_rst_n,
    input  logic [7:0] i_cfg,
    output logic [7:0] o_status
);
    assign o_status = i_cfg;
endmodule

module noreset_block (
    input  logic clk,
    input  logic [3:0] i_val,
    output logic [3:0] o_val
);
    assign o_val = i_val;
endmodule

module clk_rst_top (
    input  logic sys_clk,
    input  logic peri_clk,
    input  logic sys_rst_n,
    input  logic peri_rst_n
);
    logic [31:0] core_data;
    logic [7:0]  periph_status;
    logic [3:0]  nr_val;

    core u_core (
        .sys_clk(sys_clk), .sys_rst_n(sys_rst_n),
        .i_data(32'h0), .o_data(core_data)
    );

    periph u_periph (
        .peri_clk(peri_clk), .peri_rst_n(peri_rst_n),
        .i_cfg(8'h0), .o_status(periph_status)
    );

    noreset_block u_norst (
        .clk(sys_clk),
        .i_val(4'h0), .o_val(nr_val)
    );
endmodule
