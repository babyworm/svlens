// svlens: waive USR-001
module user_rules_inline_top(input logic i_data, output logic o_data);
    // svlens: waive USR-002 reason: retained fixture signal
    logic tmp_data;
    assign tmp_data = i_data;
    assign o_data = tmp_data;
endmodule
