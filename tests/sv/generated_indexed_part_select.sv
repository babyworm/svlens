module generated_part_source(output logic [39:0] data_o);
endmodule

module generated_part_sink(input logic [7:0] data_i);
endmodule

module generated_part_ascending_source(output logic [0:31] data_o);
endmodule

module generated_indexed_part_select_top(input logic [5:0] dynamic_i);
    localparam int unsigned LaneWidth = 8;
    localparam int unsigned SourcePitch = 10;
    logic [39:0] source;
    logic [31:0] gathered;
    logic [0:31] ascending;
    generated_part_source u_source(.data_o(source));
    generated_part_ascending_source u_ascending_source(.data_o(ascending));
    for (genvar bank = 0; bank < 4; bank++) begin : gen_banks
        assign gathered[bank*LaneWidth+:LaneWidth] = source[bank*SourcePitch+:LaneWidth];
        generated_part_sink u_sink(.data_i(gathered[bank*LaneWidth+:LaneWidth]));
        generated_part_sink u_down(.data_i(source[bank*SourcePitch+LaneWidth-1-:LaneWidth]));
        generated_part_sink u_ascending(.data_i(ascending[bank*LaneWidth+:LaneWidth]));
    end
    generated_part_sink u_dynamic(.data_i(source[dynamic_i+:LaneWidth]));
endmodule
