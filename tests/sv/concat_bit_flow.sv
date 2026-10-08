module concat_source4(output logic [3:0] o_data);
    assign o_data = 4'hA;
endmodule

module concat_source8(output logic [7:0] o_data);
    assign o_data = 8'hA5;
endmodule

module concat_sink4(input logic [3:0] i_data);
endmodule

module concat_sink2(input logic [1:0] i_data);
endmodule

module concat_sink6(input logic [5:0] i_data);
endmodule

module concat_sink8(input logic [7:0] i_data);
endmodule

module concat_select_source(output logic o_select);
    assign o_select = 1'b1;
endmodule

module concat_bit_flow_top;
    logic [3:0] a, b, pair_hi, pair_lo;
    logic [7:0] wide;
    logic [3:0] wide_hi, wide_lo, chained_hi, chained_lo;
    logic [3:0] procedural_hi, procedural_lo;
    logic [1:0] unequal_hi;
    logic [5:0] unequal_lo;
    logic [3:0] constant_hi, constant_lo;
    logic select_signal;
    logic [3:0] selected_hi, selected_lo;
    logic [3:0] arithmetic_hi, arithmetic_lo;
    logic [3:0] chained_alias;
    logic [7:0] assembled;
    logic [7:0] reversed;
    concat_source4 u_a(.o_data(a));
    concat_source4 u_b(.o_data(b));
    concat_source8 u_wide(.o_data(wide));
    concat_select_source u_select(.o_select(select_signal));
    assign {pair_hi, pair_lo} = {a, b};
    assign {wide_hi, wide_lo} = wide;
    assign {chained_hi, chained_lo} = {wide_hi, wide_lo};
    assign chained_alias = chained_hi;
    assign {assembled[7:4], assembled[3:0]} = {a, b};
    assign reversed = {wide[0], wide[1], wide[2], wide[3],
                       wide[4], wide[5], wide[6], wide[7]};
    always_comb {procedural_hi, procedural_lo} = {a, b};
    assign {unequal_hi, unequal_lo} = wide;
    assign {constant_hi, constant_lo} = {wide[7:6], {2'b00, wide[3:0]}};
    always_comb begin
        if (select_signal)
            {selected_hi, selected_lo} = {a, b};
        else
            {selected_hi, selected_lo} = {b, a};
    end
    assign {arithmetic_hi, arithmetic_lo} = {a + b, a};
    concat_sink4 u_pair_hi(.i_data(pair_hi));
    concat_sink4 u_pair_lo(.i_data(pair_lo));
    concat_sink4 u_wide_hi(.i_data(wide_hi));
    concat_sink4 u_wide_lo(.i_data(wide_lo));
    concat_sink4 u_chained_hi(.i_data(chained_hi));
    concat_sink4 u_chained_lo(.i_data(chained_lo));
    concat_sink4 u_chained_alias(.i_data(chained_alias));
    concat_sink8 u_assembled(.i_data(assembled));
    concat_sink8 u_reversed(.i_data(reversed));
    concat_sink4 u_procedural_hi(.i_data(procedural_hi));
    concat_sink4 u_procedural_lo(.i_data(procedural_lo));
    concat_sink2 u_unequal_hi(.i_data(unequal_hi));
    concat_sink6 u_unequal_lo(.i_data(unequal_lo));
    concat_sink4 u_constant_hi(.i_data(constant_hi));
    concat_sink4 u_constant_lo(.i_data(constant_lo));
    concat_sink4 u_selected_hi(.i_data(selected_hi));
    concat_sink4 u_selected_lo(.i_data(selected_lo));
    concat_sink4 u_arithmetic_hi(.i_data(arithmetic_hi));
    concat_sink4 u_arithmetic_lo(.i_data(arithmetic_lo));
endmodule
