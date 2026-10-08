module fanout_shared(
    input logic a,
    input logic b,
    output logic y0,
    output logic y1
);
    logic shared;
    assign shared = a ^ b;
    assign y0 = shared;
    assign y1 = shared;
endmodule
