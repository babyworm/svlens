interface generated_bus;
    logic [7:0] data;
    logic valid;
endinterface

module generated_writer(generated_bus bus, input logic [7:0] data_i);
    if (1) begin : g_write
        assign bus.data = data_i;
        assign bus.valid = 1'b1;
    end
endmodule

module generated_reader(generated_bus bus, output logic [7:0] data_o);
    for (genvar i = 0; i < 1; i++) begin : g_read
        always_comb data_o = bus.data;
    end
endmodule

module interface_whole_generate;
    generated_bus shared();
    generated_bus other();
    logic [7:0] source_data, observed, observed_other;
    generated_writer u_prod(.bus(shared), .data_i(source_data));
    generated_reader u_cons(.bus(shared), .data_o(observed));
    generated_writer u_other_prod(.bus(other), .data_i(source_data));
    generated_reader u_other_cons(.bus(other), .data_o(observed_other));
endmodule
