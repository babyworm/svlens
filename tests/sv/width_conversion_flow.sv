module width_flow_source4(output logic [3:0] o_data);
    assign o_data = 4'h9;
endmodule

module width_flow_signed_source4(output logic signed [3:0] o_data);
    assign o_data = -4'sd3;
endmodule

module width_flow_source8(output logic [7:0] o_data);
    assign o_data = 8'ha5;
endmodule

module width_flow_ascending_source4(output logic signed [0:3] o_data);
    assign o_data = -4'sd3;
endmodule

module width_flow_sink8(input logic [7:0] i_data);
endmodule

module width_flow_sink4(input logic [3:0] i_data);
endmodule

module width_flow_sink6(input logic [5:0] i_data);
endmodule

module width_flow_sink10(input logic [9:0] i_data);
endmodule

module width_conversion_flow_top;
    logic [3:0] unsigned_data;
    logic signed [3:0] signed_data;
    logic signed [0:3] ascending_signed_data;
    logic [7:0] wide_data;
    logic [7:0] zero_extended, cast_extended, sliced_signed_extended;
    logic signed [7:0] sign_extended, ascending_sign_extended, cast_signed_extended;
    logic [3:0] truncated, cast_truncated;
    logic [5:0] cast_concat_truncated;
    logic [9:0] cast_concat_extended;

    width_flow_source4 u_unsigned(.o_data(unsigned_data));
    width_flow_signed_source4 u_signed(.o_data(signed_data));
    width_flow_ascending_source4 u_ascending_signed(.o_data(ascending_signed_data));
    width_flow_source8 u_wide(.o_data(wide_data));
    assign zero_extended = unsigned_data[3:0];
    assign sign_extended = signed_data;
    assign ascending_sign_extended = ascending_signed_data;
    assign sliced_signed_extended = signed_data[3:0];
    assign truncated = wide_data[7:0];
    assign cast_extended = 8'(unsigned_data[3:0]);
    assign cast_signed_extended = 8'(signed_data);
    assign cast_truncated = 4'(wide_data);
    assign cast_concat_truncated = 6'({unsigned_data, wide_data[3:0]});
    assign cast_concat_extended = 10'({unsigned_data, signed_data});
    width_flow_sink8 u_zero(.i_data(zero_extended));
    width_flow_sink8 u_sign(.i_data(sign_extended));
    width_flow_sink8 u_ascending_sign(.i_data(ascending_sign_extended));
    width_flow_sink8 u_signed_slice(.i_data(sliced_signed_extended));
    width_flow_sink4 u_truncated(.i_data(truncated));
    width_flow_sink8 u_cast(.i_data(cast_extended));
    width_flow_sink8 u_cast_signed(.i_data(cast_signed_extended));
    width_flow_sink4 u_cast_truncated(.i_data(cast_truncated));
    width_flow_sink6 u_cast_concat_truncated(.i_data(cast_concat_truncated));
    width_flow_sink10 u_cast_concat_extended(.i_data(cast_concat_extended));
endmodule
