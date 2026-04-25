create_clock -name ca -period 10.0 [get_ports ca]
create_generated_clock -name gated_clk -source [get_ports ca] [get_pins u_icg/clk_out]
