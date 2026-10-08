create_clock -name clk_a -period 10 [get_ports clk_a]
create_clock -name clk_b -period 12 [get_ports clk_b]
set_max_delay abc -from [get_clocks clk_a] -to [get_clocks clk_b]
