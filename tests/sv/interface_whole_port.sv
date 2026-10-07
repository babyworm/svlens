interface whole_bus;
    logic [7:0] data;
    logic valid;
endinterface

module whole_producer(whole_bus bus);
    always_comb bus.data = 8'h3c;
    assign bus.valid = 1'b1;
endmodule

module whole_consumer(whole_bus bus, output logic [7:0] observed);
    always_comb observed = bus.data;
endmodule

module interface_whole_port;
    whole_bus shared();
    whole_bus other();
    logic [7:0] observed;
    logic [7:0] observed_other;
    whole_producer u_prod(.bus(shared));
    whole_consumer u_cons(.bus(shared), .observed(observed));
    whole_producer u_other_prod(.bus(other));
    whole_consumer u_other_cons(.bus(other), .observed(observed_other));
endmodule
