// Header file for fixture 39 — define-driven 2-FF synchronizer.
// Demonstrates that the -D macro define and -I include path support
// works for downstream RTL projects that vendor their synchronizers
// behind preprocessor gating (VeeR / BlackParrot pattern).

`ifndef MACRO_SYNC_CHAIN_SVH
`define MACRO_SYNC_CHAIN_SVH

`define SYNC_2FF(name, w, src, dst, clk, rstn) \
    logic [w-1:0] name``_meta_q; \
    logic [w-1:0] name``_q; \
    always_ff @(posedge clk or negedge rstn) begin \
        if (!rstn) begin \
            name``_meta_q <= '0; \
            name``_q      <= '0; \
        end else begin \
            name``_meta_q <= src; \
            name``_q      <= name``_meta_q; \
        end \
    end \
    assign dst = name``_q;

`endif
