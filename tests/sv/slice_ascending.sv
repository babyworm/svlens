module ascending_source(output logic [7:0] o_data);
    assign o_data = 8'hA5;
endmodule

module ascending_sink(input logic [3:0] i_data);
endmodule

module slice_ascending_top;
    logic [0:7] ascending_bus;
    ascending_source u_src(.o_data(ascending_bus));
    ascending_sink u_high(.i_data(ascending_bus[0:3]));
    ascending_sink u_low(.i_data(ascending_bus[4:7]));
endmodule
