// metrics-mode fixture combining for-loop iteration with LHS
// concatenation. This is a common barrel-shift / per-lane datapath
// idiom where for-loop unrolling and concat decomposition interact:
//
//   * The for-loop body assigns to a sliced output (LHS slice),
//     so each iteration adds one Slice + Binary node pair.
//   * The interleaved {hi, lo} concat assignment adds a multi-LHS
//     decomposition that should expose the lane-group normalizer.
//
// Acceptance (codified in test_metrics_lhs_concat_for_loop.sh):
//   raw_node_count >= 8  (4 lane iterations + 2 concat-LHS slices
//                         + at least 2 binary ops in the chain)
//   logic_depth_est >= 2 (XOR after slice)
//   normalized_transform_count <= raw_node_count (lane-group fold)
//   no unsupported_events
//
// If a future change to TransformExtractor drops below these bounds,
// the test will fail and we will be alerted to the regression.

module lhs_concat_for_loop (
    input  logic [31:0] data_in,
    input  logic [7:0]  mask_in,
    output logic [31:0] data_out,
    output logic [7:0]  hi,
    output logic [7:0]  lo
);

    always_comb begin
        for (int i = 0; i < 4; i++) begin
            data_out[i*8 +: 8] = data_in[i*8 +: 8] ^ {8{mask_in[i]}};
        end
    end

    // LHS concat decomposition: each operand gets its own driver.
    assign {hi, lo} = {mask_in, ~mask_in};

endmodule
