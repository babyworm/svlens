create_clock -name fast_clk -period 10 [get_ports clk_fast]
create_generated_clock -name slow_clk -source [get_ports clk_fast] -divide_by 2 [get_pins u_div/clk_o]
