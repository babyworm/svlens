module user_rules_child;
endmodule

module user_rules_top(input logic i_data, output logic o_data);
    logic tmp_data;
    user_rules_child u_a();
    user_rules_child u_b();
    assign tmp_data = i_data;
    assign o_data = tmp_data;
endmodule
