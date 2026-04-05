interface data_if(input logic clk);
    logic [7:0] data;
    logic valid;

    modport producer (
        input  clk,
        output data,
        output valid
    );

    modport consumer (
        input clk,
        input data,
        input valid
    );
endinterface

module producer(data_if.producer bus);
    always_comb begin
        bus.data = 8'hA5;
        bus.valid = 1'b1;
    end
endmodule

module consumer(data_if.consumer bus);
endmodule

module interface_modport_top(input logic clk);
    data_if bus(clk);

    producer u_prod(.bus(bus));
    consumer u_cons(.bus(bus));
endmodule
