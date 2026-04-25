// conn-mode negative pair for conn_modport_width_pos.
// Same hierarchy, but consumer port matches the modport signal
// width. WidthChecker should NOT fire.

interface narrow_bus;
    logic [7:0] data;
    logic       valid;
    modport master (output data, output valid);
    modport slave  (input  data, input  valid);
endinterface

module narrow_consumer (
    input  logic [7:0] narrow_data,
    input  logic       narrow_valid
);
endmodule

module bridge_glue (narrow_bus.slave bus);
    narrow_consumer u_nc (
        .narrow_data  (bus.data),  // 8-bit to 8-bit, clean
        .narrow_valid (bus.valid)
    );
endmodule

module producer (narrow_bus.master bus);
    assign bus.data  = 8'hAB;
    assign bus.valid = 1'b1;
endmodule

module conn_modport_width_neg (
    input  logic clk,
    input  logic rst_n
);
    narrow_bus inst();
    producer    u_prod (.bus(inst));
    bridge_glue u_glue (.bus(inst));
endmodule
