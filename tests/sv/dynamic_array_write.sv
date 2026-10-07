module dynamic_write_source(output logic [7:0] o_data);
endmodule

module dynamic_write_sink(input logic [7:0] i_data);
endmodule

module dynamic_write_whole_source(output logic [7:0] o_data [0:1]);
endmodule

module dynamic_array_write_top(input logic bank_i);
    logic [7:0] driving;
    logic [7:0] bank [0:1];
    logic [7:0] port_bank [0:1];
    dynamic_write_source u_source(.o_data(driving));
    always_comb begin
        bank[0] = '0;
        bank[1] = '0;
        bank[bank_i] = driving;
    end
    dynamic_write_sink u_zero(.i_data(bank[0]));
    dynamic_write_sink u_one(.i_data(bank[1]));
    dynamic_write_whole_source u_port_source(.o_data(port_bank));
    dynamic_write_sink u_port_zero(.i_data(port_bank[0]));
    dynamic_write_sink u_port_one(.i_data(port_bank[1]));
    dynamic_write_sink u_port_dynamic(.i_data(port_bank[bank_i]));
endmodule
