interface slice_bus;
    logic [15:0] data;
    modport producer (output data);
    modport consumer (input data);
endinterface

module slice_writer(slice_bus bus, input logic [7:0] lo_i);
    assign bus.data[7:0] = lo_i;
endmodule

module slice_reader(slice_bus bus, output logic [7:0] lo_o, hi_o);
    assign lo_o = bus.data[7:0];
    assign hi_o = bus.data[15:8];
endmodule

module interface_whole_slices;
    slice_bus shared();
    logic [7:0] lo_i, lo_o, hi_o;
    slice_writer u_writer(.bus(shared), .lo_i(lo_i));
    slice_reader u_reader(.bus(shared), .lo_o(lo_o), .hi_o(hi_o));
endmodule

module indexed_slice_writer(slice_bus bus, input logic [7:0] data_i);
    localparam int Base = 4;
    assign bus.data[Base+:8] = data_i;
endmodule

module indexed_slice_reader(slice_bus.consumer bus, output logic [7:0] data_o);
    assign data_o = bus.data[11-:8];
endmodule

module interface_indexed_slices;
    slice_bus shared();
    logic [7:0] data_i, data_o;
    indexed_slice_writer u_writer(.bus(shared), .data_i(data_i));
    indexed_slice_reader u_reader(.bus(shared), .data_o(data_o));
endmodule

module slice_high_writer(slice_bus bus, input logic [7:0] hi_i);
    assign bus.data[15:8] = hi_i;
endmodule

module interface_whole_high_slices;
    slice_bus shared();
    logic [7:0] hi_i, lo_o, hi_o;
    slice_high_writer u_writer(.bus(shared), .hi_i(hi_i));
    slice_reader u_reader(.bus(shared), .lo_o(lo_o), .hi_o(hi_o));
endmodule

module slice_expr_reader(slice_bus bus, output logic [7:0] lo_o);
    assign lo_o = bus.data[7:0] ^ 8'hff;
endmodule

module slice_full_writer(slice_bus bus, input logic [15:0] data_i);
    assign bus.data = data_i;
endmodule

module interface_whole_expr_slices;
    slice_bus shared();
    logic [15:0] data_i;
    logic [7:0] lo_o;
    slice_full_writer u_writer(.bus(shared), .data_i(data_i));
    slice_expr_reader u_reader(.bus(shared), .lo_o(lo_o));
endmodule

module slice_pair_expr_reader(slice_bus bus, output logic [3:0] data_o);
    assign data_o = bus.data[3:0] ^ bus.data[11:8];
endmodule

module interface_whole_pair_expr_slices;
    slice_bus shared();
    logic [15:0] data_i;
    logic [3:0] data_o;
    slice_full_writer u_writer(.bus(shared), .data_i(data_i));
    slice_pair_expr_reader u_reader(.bus(shared), .data_o(data_o));
endmodule

module slice_guard_reader(slice_bus bus, output logic flag_o);
    always_comb begin
        flag_o = 1'b0;
        if (bus.data[2])
            flag_o = 1'b1;
    end
endmodule

module interface_whole_guard_slices;
    slice_bus shared();
    logic [15:0] data_i;
    logic flag_o;
    slice_full_writer u_writer(.bus(shared), .data_i(data_i));
    slice_guard_reader u_reader(.bus(shared), .flag_o(flag_o));
endmodule

interface ascending_slice_bus;
    logic [0:15] data;
endinterface

module ascending_slice_writer(ascending_slice_bus bus, input logic [7:0] data_i);
    assign bus.data[0:7] = data_i;
endmodule

module ascending_slice_reader(ascending_slice_bus bus, output logic [7:0] data_o);
    assign data_o = bus.data[0:7];
endmodule

module interface_whole_ascending_slices;
    ascending_slice_bus shared();
    logic [7:0] data_i, data_o;
    ascending_slice_writer u_writer(.bus(shared), .data_i(data_i));
    ascending_slice_reader u_reader(.bus(shared), .data_o(data_o));
endmodule

module modport_full_writer(slice_bus.producer bus, input logic [15:0] data_i);
    assign bus.data = data_i;
endmodule

module interface_whole_mixed_slice;
    slice_bus shared();
    logic [15:0] data_i;
    logic [7:0] lo_o, hi_o;
    modport_full_writer u_writer(.bus(shared), .data_i(data_i));
    slice_reader u_reader(.bus(shared), .lo_o(lo_o), .hi_o(hi_o));
endmodule

module modport_slice_reader(slice_bus.consumer bus, output logic [7:0] lo_o);
    assign lo_o = bus.data[7:0];
endmodule

module interface_whole_reverse_mixed_slice;
    slice_bus shared();
    logic [7:0] lo_i, lo_o;
    slice_writer u_writer(.bus(shared), .lo_i(lo_i));
    modport_slice_reader u_reader(.bus(shared), .lo_o(lo_o));
endmodule

module slice_wrapper(slice_bus bus, output logic [7:0] lo_o, hi_o);
    slice_reader u_leaf(.bus(bus), .lo_o(lo_o), .hi_o(hi_o));
endmodule

module interface_whole_nested_slices;
    slice_bus shared();
    logic [7:0] lo_i, lo_o, hi_o;
    slice_writer u_writer(.bus(shared), .lo_i(lo_i));
    slice_wrapper u_wrap(.bus(shared), .lo_o(lo_o), .hi_o(hi_o));
endmodule

module slice_bridge(slice_bus in_bus, slice_bus out_bus);
    assign out_bus.data[7:0] = in_bus.data[15:8];
endmodule

module interface_whole_slice_bridge;
    slice_bus upstream();
    slice_bus downstream();
    slice_bus other_upstream();
    slice_bus other_downstream();
    logic [7:0] hi_i, lo_o, hi_o;
    logic [7:0] other_hi_i, other_lo_o, other_hi_o;
    slice_high_writer u_writer(.bus(upstream), .hi_i(hi_i));
    slice_bridge u_bridge(.in_bus(upstream), .out_bus(downstream));
    slice_reader u_reader(.bus(downstream), .lo_o(lo_o), .hi_o(hi_o));
    slice_high_writer u_other_writer(.bus(other_upstream), .hi_i(other_hi_i));
    slice_bridge u_other_bridge(.in_bus(other_upstream), .out_bus(other_downstream));
    slice_reader u_other_reader(.bus(other_downstream), .lo_o(other_lo_o), .hi_o(other_hi_o));
endmodule

module slice_bridge_up(slice_bus in_bus, slice_bus out_bus);
    assign out_bus.data[15:8] = in_bus.data[7:0];
endmodule

module interface_whole_slice_chain;
    slice_bus upstream();
    slice_bus middle();
    slice_bus downstream();
    logic [7:0] hi_i, lo_o, hi_o;
    slice_high_writer u_writer(.bus(upstream), .hi_i(hi_i));
    slice_bridge u_first(.in_bus(upstream), .out_bus(middle));
    slice_bridge_up u_second(.in_bus(middle), .out_bus(downstream));
    slice_reader u_reader(.bus(downstream), .lo_o(lo_o), .hi_o(hi_o));
endmodule

module slice_branch_bridge(slice_bus in_bus, slice_bus out_bus, input logic select_i);
    always_comb begin
        out_bus.data[7:0] = '0;
        if (select_i)
            out_bus.data[7:0] = in_bus.data[15:8];
    end
endmodule

module interface_whole_branch_bridge;
    slice_bus upstream();
    slice_bus downstream();
    logic [7:0] hi_i, lo_o, hi_o;
    logic select_i;
    slice_high_writer u_writer(.bus(upstream), .hi_i(hi_i));
    slice_branch_bridge u_bridge(.in_bus(upstream), .out_bus(downstream), .select_i(select_i));
    slice_reader u_reader(.bus(downstream), .lo_o(lo_o), .hi_o(hi_o));
endmodule

module slice_mux_bridge(slice_bus in_a, slice_bus in_b, slice_bus out_bus, input logic select_i);
    assign out_bus.data[7:0] = select_i ? in_a.data[15:8] : in_b.data[15:8];
endmodule

module interface_whole_mux_bridge;
    slice_bus upstream_a();
    slice_bus upstream_b();
    slice_bus downstream();
    logic [7:0] hi_a, hi_b, lo_o, hi_o;
    logic select_i;
    slice_high_writer u_writer_a(.bus(upstream_a), .hi_i(hi_a));
    slice_high_writer u_writer_b(.bus(upstream_b), .hi_i(hi_b));
    slice_mux_bridge u_bridge(.in_a(upstream_a), .in_b(upstream_b),
                              .out_bus(downstream), .select_i(select_i));
    slice_reader u_reader(.bus(downstream), .lo_o(lo_o), .hi_o(hi_o));
endmodule

module slice_const_mux_bridge(slice_bus in_a, slice_bus in_b, slice_bus out_bus);
    assign out_bus.data[7:0] = 1'b1 ? in_a.data[15:8] : in_b.data[15:8];
endmodule

module interface_whole_const_mux_bridge;
    slice_bus upstream_a();
    slice_bus upstream_b();
    slice_bus downstream();
    logic [7:0] hi_a, hi_b, lo_o, hi_o;
    slice_high_writer u_writer_a(.bus(upstream_a), .hi_i(hi_a));
    slice_high_writer u_writer_b(.bus(upstream_b), .hi_i(hi_b));
    slice_const_mux_bridge u_bridge(.in_a(upstream_a), .in_b(upstream_b), .out_bus(downstream));
    slice_reader u_reader(.bus(downstream), .lo_o(lo_o), .hi_o(hi_o));
endmodule

module slice_const_if_bridge(slice_bus in_a, slice_bus in_b, slice_bus out_bus);
    localparam bit UseA = 1'b0;
    always_comb begin
        if (UseA)
            out_bus.data[7:0] = in_a.data[15:8];
        else
            out_bus.data[7:0] = in_b.data[15:8];
    end
endmodule

module interface_whole_const_if_bridge;
    slice_bus upstream_a();
    slice_bus upstream_b();
    slice_bus downstream();
    logic [7:0] hi_a, hi_b, lo_o, hi_o;
    slice_high_writer u_writer_a(.bus(upstream_a), .hi_i(hi_a));
    slice_high_writer u_writer_b(.bus(upstream_b), .hi_i(hi_b));
    slice_const_if_bridge u_bridge(.in_a(upstream_a), .in_b(upstream_b), .out_bus(downstream));
    slice_reader u_reader(.bus(downstream), .lo_o(lo_o), .hi_o(hi_o));
endmodule

module slice_const_case_bridge(slice_bus in_a, slice_bus in_b, slice_bus out_bus);
    localparam logic [1:0] Selected = 2'b10;
    always_comb begin
        case (Selected)
            2'b00: out_bus.data[7:0] = in_a.data[15:8];
            2'b10: out_bus.data[7:0] = in_b.data[15:8];
            default: out_bus.data[7:0] = '0;
        endcase
    end
endmodule

module interface_whole_const_case_bridge;
    slice_bus upstream_a();
    slice_bus upstream_b();
    slice_bus downstream();
    logic [7:0] hi_a, hi_b, lo_o, hi_o;
    slice_high_writer u_writer_a(.bus(upstream_a), .hi_i(hi_a));
    slice_high_writer u_writer_b(.bus(upstream_b), .hi_i(hi_b));
    slice_const_case_bridge u_bridge(.in_a(upstream_a), .in_b(upstream_b), .out_bus(downstream));
    slice_reader u_reader(.bus(downstream), .lo_o(lo_o), .hi_o(hi_o));
endmodule

module slice_const_casez_bridge(slice_bus in_a, slice_bus in_b, slice_bus out_bus);
    localparam logic [1:0] Selected = 2'b10;
    always_comb begin
        casez (Selected)
            2'b0?: out_bus.data[7:0] = in_a.data[15:8];
            2'b1?: out_bus.data[7:0] = in_b.data[15:8];
            default: out_bus.data[7:0] = '0;
        endcase
    end
endmodule

module interface_whole_const_casez_bridge;
    slice_bus upstream_a();
    slice_bus upstream_b();
    slice_bus downstream();
    logic [7:0] hi_a, hi_b, lo_o, hi_o;
    slice_high_writer u_writer_a(.bus(upstream_a), .hi_i(hi_a));
    slice_high_writer u_writer_b(.bus(upstream_b), .hi_i(hi_b));
    slice_const_casez_bridge u_bridge(.in_a(upstream_a), .in_b(upstream_b), .out_bus(downstream));
    slice_reader u_reader(.bus(downstream), .lo_o(lo_o), .hi_o(hi_o));
endmodule
