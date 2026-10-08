module mux_glue_data_source(output logic [7:0] o_data);
    assign o_data = 8'h5a;
endmodule

module mux_glue_select_source(output logic o_select);
    assign o_select = 1'b1;
endmodule

module mux_glue_sink(input logic [7:0] i_data);
endmodule

module procedural_mux_glue_top;
    logic [7:0] data_a, data_b, mux_data, case_data, ternary_data, legacy_data;
    logic select;

    mux_glue_data_source u_a(.o_data(data_a));
    mux_glue_data_source u_b(.o_data(data_b));
    mux_glue_select_source u_select(.o_select(select));

    always_comb begin
        if (select)
            mux_data = data_a;
        else
            mux_data = data_b;

        case (select)
            1'b0: case_data = data_a;
            default: case_data = data_b;
        endcase

        ternary_data = select ? data_a : data_b;
    end

    always @* begin
        legacy_data = select ? data_a : data_b;
    end

    mux_glue_sink u_mux(.i_data(mux_data));
    mux_glue_sink u_case(.i_data(case_data));
    mux_glue_sink u_ternary(.i_data(ternary_data));
    mux_glue_sink u_legacy(.i_data(legacy_data));
    mux_glue_sink u_tap(.i_data(data_a));
endmodule
