# Benchmark-only projection of four base-clock periods from the pinned
# OpenTitan earlgrey_silver_release_v5 ASIC synthesis constraints.
# hw/top_earlgrey/syn/asic.constraints.sdc sets MAIN=10*0.85,
# USB=20.8*0.95, IO=10.416*0.95, AON=5000*0.95 ns.
# chip_earlgrey_asic.sv:1088-1091 directly connects ast_base_clks
# clk_sys/clk_io/clk_usb/clk_aon to top_earlgrey clk_*_i.
# This is timing context for a source-only submodule benchmark, NOT a
# sign-off SDC. It omits SPI/JTAG, generated clocks, modes, I/O delays,
# uncertainty, clock groups, false paths, and physical path delays.
create_clock -name MAIN_CLK -period 8.5 [get_ports clk_main_i]
create_clock -name IO_CLK -period 9.8952 [get_ports clk_io_i]
create_clock -name USB_CLK -period 19.76 [get_ports clk_usb_i]
create_clock -name AON_CLK -period 4750.0 [get_ports clk_aon_i]
