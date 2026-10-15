module port_alias_source(output logic [7:0] data_o);
    assign data_o = 8'hA5;
endmodule

module port_alias_glue(input logic [7:0] data_i, unused_i,
                       output logic [7:0] data_o);
    assign data_o = data_i;
endmodule

module port_alias_sink(input logic [7:0] data_i);
endmodule

module port_alias_forward_top;
    logic [7:0] first, second, third, unrelated;
    port_alias_source u_source(.data_o(first));
    port_alias_source u_unrelated(.data_o(unrelated));
    port_alias_glue u_glue0(.data_i(first), .unused_i(unrelated), .data_o(second));
    port_alias_glue u_glue1(.data_i(second), .unused_i(unrelated), .data_o(third));
    port_alias_sink u_sink(.data_i(third));
endmodule

module port_alias_register(input logic clk_i, input logic [7:0] data_i,
                           output logic [7:0] data_o);
    always_ff @(posedge clk_i) data_o <= data_i;
endmodule

module port_alias_registered_top(input logic clk_i);
    logic [7:0] source_bus, dest_bus;
    port_alias_source u_source(.data_o(source_bus));
    port_alias_register u_register(.clk_i, .data_i(source_bus), .data_o(dest_bus));
    port_alias_sink u_sink(.data_i(dest_bus));
endmodule
