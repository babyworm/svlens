// tests/sv/multi_module_soc.sv
// 3-level hierarchy: soc_top -> {u_cpu, u_bus, u_mem}
// u_cpu has sub-instance u_alu

module alu (
    input  logic        clk,
    input  logic        rst_n,
    input  logic [31:0] i_op_a,
    input  logic [31:0] i_op_b,
    input  logic [2:0]  i_op_sel,
    output logic [31:0] o_result
);
    assign o_result = i_op_a + i_op_b;
endmodule

module cpu (
    input  logic        clk,
    input  logic        rst_n,
    output logic [31:0] o_ibus_addr,
    input  logic [31:0] i_ibus_rdata,
    output logic [15:0] o_dbus_addr,
    output logic [31:0] o_dbus_wdata,
    input  logic [31:0] i_dbus_rdata,
    output logic        o_dbus_we
);
    logic [31:0] alu_a, alu_b, alu_result;
    logic [2:0]  alu_sel;

    alu u_alu (
        .clk(clk), .rst_n(rst_n),
        .i_op_a(alu_a), .i_op_b(alu_b),
        .i_op_sel(alu_sel), .o_result(alu_result)
    );

    assign o_ibus_addr = 32'h0;
    assign o_dbus_addr = 16'h0;
    assign o_dbus_wdata = alu_result;
    assign o_dbus_we = 1'b0;
endmodule

module bus_interconnect (
    input  logic        clk,
    input  logic        rst_n,
    input  logic [31:0] i_cpu_ibus_addr,
    output logic [31:0] o_cpu_ibus_rdata,
    input  logic [15:0] i_cpu_dbus_addr,
    input  logic [31:0] i_cpu_dbus_wdata,
    output logic [31:0] o_cpu_dbus_rdata,
    input  logic        i_cpu_dbus_we,
    output logic [31:0] o_mem_addr,
    output logic [31:0] o_mem_wdata,
    input  logic [31:0] i_mem_rdata,
    output logic        o_mem_we
);
    assign o_mem_addr  = {16'h0, i_cpu_dbus_addr};
    assign o_mem_wdata = i_cpu_dbus_wdata;
    assign o_cpu_dbus_rdata = i_mem_rdata;
    assign o_cpu_ibus_rdata = i_mem_rdata;
    assign o_mem_we = i_cpu_dbus_we;
endmodule

module memory (
    input  logic        clk,
    input  logic        rst_n,
    input  logic [31:0] i_addr,
    input  logic [31:0] i_wdata,
    output logic [31:0] o_rdata,
    input  logic        i_we
);
    assign o_rdata = 32'h0;
endmodule

module soc_top (
    input  logic clk,
    input  logic rst_n
);
    logic [31:0] cpu_ibus_addr, cpu_ibus_rdata;
    logic [15:0] cpu_dbus_addr;
    logic [31:0] cpu_dbus_wdata, cpu_dbus_rdata;
    logic        cpu_dbus_we;
    logic [31:0] mem_addr, mem_wdata, mem_rdata;
    logic        mem_we;

    cpu u_cpu (
        .clk(clk), .rst_n(rst_n),
        .o_ibus_addr(cpu_ibus_addr),
        .i_ibus_rdata(cpu_ibus_rdata),
        .o_dbus_addr(cpu_dbus_addr),
        .o_dbus_wdata(cpu_dbus_wdata),
        .i_dbus_rdata(cpu_dbus_rdata),
        .o_dbus_we(cpu_dbus_we)
    );

    bus_interconnect u_bus (
        .clk(clk), .rst_n(rst_n),
        .i_cpu_ibus_addr(cpu_ibus_addr),
        .o_cpu_ibus_rdata(cpu_ibus_rdata),
        .i_cpu_dbus_addr(cpu_dbus_addr),
        .i_cpu_dbus_wdata(cpu_dbus_wdata),
        .o_cpu_dbus_rdata(cpu_dbus_rdata),
        .i_cpu_dbus_we(cpu_dbus_we),
        .o_mem_addr(mem_addr),
        .o_mem_wdata(mem_wdata),
        .i_mem_rdata(mem_rdata),
        .o_mem_we(mem_we)
    );

    memory u_mem (
        .clk(clk), .rst_n(rst_n),
        .i_addr(mem_addr),
        .i_wdata(mem_wdata),
        .o_rdata(mem_rdata),
        .i_we(mem_we)
    );
endmodule
