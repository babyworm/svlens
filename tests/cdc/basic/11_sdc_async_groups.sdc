# SDC companion for fixture 11_sdc_async_groups.sv.
# Declares clk_a and clk_b as asynchronous clock groups so the tool can
# treat clk_a -> clk_b crossings as genuinely async rather than having
# to guess from naming heuristics.

create_clock -name clk_a -period 10.0 [get_ports clk_a]
create_clock -name clk_b -period 7.5  [get_ports clk_b]

set_clock_groups -asynchronous -group {clk_a} -group {clk_b}
