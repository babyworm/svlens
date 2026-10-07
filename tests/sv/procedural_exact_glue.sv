module procedural_exact_source(output logic [7:0] data_o);
    assign data_o = 8'hac;
endmodule

module procedural_exact_sink8(input logic [7:0] data_i);
endmodule

module procedural_exact_sink4(input logic [3:0] data_i);
endmodule

module procedural_exact_glue(input logic select_i);
    logic [7:0] source_data, whole_data;
    logic [3:0] high_data, low_data, overwritten_data, conditional_data, compound_data;

    procedural_exact_source u_source(.data_o(source_data));

    always_comb whole_data = source_data;
    always_comb high_data = source_data[7:4];
    always @* low_data = source_data[3:0];

    always_comb begin
        overwritten_data = source_data[7:4];
        overwritten_data = source_data[3:0];
    end

    always_comb begin
        if (select_i)
            conditional_data = source_data[7:4];
        else
            conditional_data = 4'h0;
    end

    always_comb compound_data += source_data[3:0];

    procedural_exact_sink8 u_whole(.data_i(whole_data));
    procedural_exact_sink4 u_high(.data_i(high_data));
    procedural_exact_sink4 u_low(.data_i(low_data));
    procedural_exact_sink4 u_overwritten(.data_i(overwritten_data));
    procedural_exact_sink4 u_conditional(.data_i(conditional_data));
    procedural_exact_sink4 u_compound(.data_i(compound_data));
endmodule
