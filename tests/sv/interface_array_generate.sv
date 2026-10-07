interface generated_array_bus #(parameter int Width = 12);
    logic [Width-1:0] data;
    modport master (output data);
    modport slave (input data);
endinterface

module generated_array_writer(generated_array_bus.master bus);
    assign bus.data = '0;
endmodule

module generated_array_reader(generated_array_bus.slave bus);
endmodule

module interface_array_generate #(parameter int Count = 2);
    generated_array_bus #(.Width(12)) buses[Count]();
    for (genvar i = 0; i < Count; i++) begin : g_lane
        generated_array_writer u_w(.bus(buses[i]));
        generated_array_reader u_r(.bus(buses[i]));
    end
endmodule
