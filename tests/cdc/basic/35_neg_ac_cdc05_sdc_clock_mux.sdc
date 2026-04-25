# SDC companion for fixture 35_neg_ac_cdc05_sdc_clock_mux.sv. By
# declaring clk_sdc_gen as a generated clock, the user asserts it is
# a properly modelled clock; the Ac_cdc05 detector skips sources of
# type Generated.

create_clock -name clk_a -period 10.0 [get_ports clk_a]
create_clock -name clk_b -period 7.5  [get_ports clk_b]

create_generated_clock -name clk_sdc_gen \
    -source [get_ports clk_a] \
    [get_pins clk_sdc_gen]

set_clock_groups -asynchronous -group {clk_a clk_sdc_gen} -group {clk_b}
