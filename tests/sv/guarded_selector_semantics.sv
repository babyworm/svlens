module guarded_selector_source(input logic [31:0] value_i, output logic [31:0] value_o);
    assign value_o = value_i;
endmodule

module guarded_selector_sink(input logic value_i);
endmodule

module guarded_selector_semantics(input logic [31:0] value_i);
    logic [31:0] value;
    logic invalid_single, invalid_multiple;
    logic single_csid, multiple_csid;
    guarded_selector_source u_source(.value_i(value_i), .value_o(value));
    assign invalid_single = value >= 32'd1;
    assign invalid_multiple = value >= 32'd2;
    assign single_csid = invalid_single ? 1'b0 : value[0];
    assign multiple_csid = invalid_multiple ? 1'b0 : value[0];
    guarded_selector_sink u_single(.value_i(single_csid));
    guarded_selector_sink u_multiple(.value_i(multiple_csid));

    // The one-target guard is constant only over binary inputs. Slang evaluates
    // these independently of the connection extractor's structural graph.
    function automatic logic guarded(input logic [31:0] candidate, input int unsigned limit);
        logic invalid;
        invalid = candidate >= limit;
        return invalid ? 1'b0 : candidate[0];
    endfunction

    localparam logic BinaryZero = guarded(32'd0, 1);
    localparam logic BinaryOne = guarded(32'd1, 1);
    localparam logic BinaryMax = guarded(32'hffff_ffff, 1);
    localparam logic MultipleOne = guarded(32'd1, 2);
    localparam logic UnknownLow = guarded(32'h0000_000x, 1);
    localparam logic HighImpedanceLow = guarded(32'h0000_000z, 1);
    localparam logic UnknownHighLowZero = guarded(32'hx000_0000, 1);
    localparam logic UnknownHighLowOne = guarded(32'hx000_0001, 1);
endmodule
