create_clock -name clk_a -period 10 [get_ports clk_a]
create_clock -name clk_b -period 12 [get_ports clk_b]
set_clock_groups -logically_exclusive -group {clk_a} -group {clk_b}
