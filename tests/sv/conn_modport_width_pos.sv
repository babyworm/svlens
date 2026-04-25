// conn-mode positive fixture: interface modport width truncation
// across a hierarchy boundary.
//
// Expected behavior under svlens conn:
//   - The producer drives an 8-bit modport signal `bus.data`.
//   - The bridge_glue module re-publishes that signal into a wider
//     16-bit consumer port `wide_data`.
//   - WidthChecker should observe the 8 -> 16 zero/sign-extension
//     hop on the .wide_data port connection inside bridge_glue.
//
// Companion: conn_modport_width_neg.sv keeps the same hierarchy but
// uses matching widths so the negative pair confirms there is no
// false positive on the structural shape itself.

interface narrow_bus;
    logic [7:0] data;
    logic       valid;
    modport master (output data, output valid);
    modport slave  (input  data, input  valid);
endinterface

module wide_consumer (
    input  logic [15:0] wide_data,
    input  logic        wide_valid
);
    // Sink only -- no driver needed; widthchecker should fire on the
    // 8->16 zero-extension at this port connection.
endmodule

module bridge_glue (narrow_bus.slave bus);
    wide_consumer u_wc (
        .wide_data  (bus.data),    // <-- WIDTH MISMATCH 8 vs 16
        .wide_valid (bus.valid)
    );
endmodule

module producer (narrow_bus.master bus);
    assign bus.data  = 8'hAB;
    assign bus.valid = 1'b1;
endmodule

module conn_modport_width_pos (
    input  logic clk,
    input  logic rst_n
);
    narrow_bus inst();
    producer    u_prod (.bus(inst));
    bridge_glue u_glue (.bus(inst));
endmodule
