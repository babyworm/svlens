module branch_slice_source(output logic [7:0] data_o);
    assign data_o = 8'hac;
endmodule

module branch_slice_select(output logic select_o);
    assign select_o = 1'b1;
endmodule

module branch_slice_sink(input logic [3:0] data_i);
endmodule

module branch_slice_sink1(input logic data_i);
endmodule

module procedural_branch_slices #(parameter bit EnableBranch = 1'b0);
    logic [7:0] data_a, data_b;
    logic select;
    logic [3:0] selected, decoded, constant_selected, constant_decoded;
    logic [3:0] constant_casez, constant_casex, computed_if, runtime_casez, runtime_casex;
    logic nested_data;

    branch_slice_source u_a(.data_o(data_a));
    branch_slice_source u_b(.data_o(data_b));
    branch_slice_select u_select(.select_o(select));

    always_comb begin
        if (select)
            selected = data_a[7:4];
        else
            selected = data_b[3:0];

        case (select)
            1'b0: decoded = data_b[7:4];
            default: decoded = data_a[3:0];
        endcase
    end

    always_comb begin
        if (1'b0)
            constant_selected = data_a[7:4];
        else
            constant_selected = data_b[3:0];
    end

    always_comb begin
        case (2'b10)
            2'b00: constant_decoded = data_a[7:4];
            2'b10: constant_decoded = data_b[3:0];
            default: constant_decoded = 4'h0;
        endcase
    end

    always_comb begin
        casez (2'b10)
            2'b0?: constant_casez = data_a[7:4];
            2'b1?: constant_casez = data_b[3:0];
            default: constant_casez = 4'h0;
        endcase
    end

    always_comb begin
        casex (2'b10)
            2'b0x: constant_casex = data_a[7:4];
            2'b1x: constant_casex = data_b[3:0];
            default: constant_casex = 4'h0;
        endcase
    end

    always_comb begin
        casez ({select, 1'b0})
            2'b0?: runtime_casez = data_a[7:4];
            2'b1?: runtime_casez = data_b[3:0];
            default: runtime_casez = 4'h0;
        endcase
    end

    always_comb begin
        casex ({select, 1'b0})
            2'b0x: runtime_casex = data_a[7:4];
            2'b1x: runtime_casex = data_b[3:0];
            default: runtime_casex = 4'h0;
        endcase
    end

    always_comb begin
        if ((2 + 3) == 4)
            computed_if = data_a[7:4];
        else
            computed_if = data_b[3:0];
    end

    always_comb begin
        nested_data = data_b[0];
        case (select)
            1'b1: if (EnableBranch) nested_data = data_a[0];
            default: ;
        endcase
    end

    branch_slice_sink u_selected(.data_i(selected));
    branch_slice_sink u_decoded(.data_i(decoded));
    branch_slice_sink u_constant(.data_i(constant_selected));
    branch_slice_sink u_constant_case(.data_i(constant_decoded));
    branch_slice_sink u_casez(.data_i(constant_casez));
    branch_slice_sink u_casex(.data_i(constant_casex));
    branch_slice_sink u_computed_if(.data_i(computed_if));
    branch_slice_sink u_runtime_casez(.data_i(runtime_casez));
    branch_slice_sink u_runtime_casex(.data_i(runtime_casex));
    branch_slice_sink1 u_nested(.data_i(nested_data));
endmodule
