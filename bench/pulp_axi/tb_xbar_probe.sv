module tb_xbar_probe;
  logic clk_i = 1'b0;
  always #5 clk_i = ~clk_i;
  logic rst_ni = 1'b0;

  logic [1:0][7:0] aw_addr_i, ar_addr_i;
  logic [1:0][2:0] aw_prot_i, ar_prot_i;
  logic [1:0][31:0] w_data_i, r_data_i;
  logic [1:0][3:0] w_strb_i;
  logic [1:0][1:0] b_resp_i, r_resp_i;
  logic [1:0] aw_valid_i, w_valid_i, ar_valid_i, b_ready_i, r_ready_i;
  logic [1:0] aw_ready_i, w_ready_i, b_valid_i, ar_ready_i, r_valid_i;

  svlens_axi_xbar_probe dut (.*);

  logic [1:0] mst_aw_valid, mst_ar_valid, mst_w_valid, mst_b_ready, mst_r_ready;
  logic [1:0][7:0] mst_aw_addr, mst_ar_addr;
  logic [1:0][2:0] mst_aw_prot, mst_ar_prot;
  logic [1:0][31:0] mst_w_data;
  logic [1:0][3:0] mst_w_strb;
  logic [1:0] slv_aw_ready, slv_w_ready, slv_ar_ready, slv_b_valid, slv_r_valid;
  logic [1:0][1:0] slv_b_resp, slv_r_resp;
  logic [1:0][31:0] slv_r_data;
  bit [4:0] stalled_channels = '0;
  int passed_cases = 0, total_writes = 0, total_reads = 0;

  for (genvar lane = 0; lane < 2; lane++) begin : gen_observe
    assign mst_aw_valid[lane] = dut.mst_if[lane].aw_valid;
    assign mst_ar_valid[lane] = dut.mst_if[lane].ar_valid;
    assign mst_w_valid[lane] = dut.mst_if[lane].w_valid;
    assign mst_aw_addr[lane] = dut.mst_if[lane].aw_addr;
    assign mst_ar_addr[lane] = dut.mst_if[lane].ar_addr;
    assign mst_aw_prot[lane] = dut.mst_if[lane].aw_prot;
    assign mst_ar_prot[lane] = dut.mst_if[lane].ar_prot;
    assign mst_w_data[lane] = dut.mst_if[lane].w_data;
    assign mst_w_strb[lane] = dut.mst_if[lane].w_strb;
    assign mst_b_ready[lane] = dut.mst_if[lane].b_ready;
    assign mst_r_ready[lane] = dut.mst_if[lane].r_ready;
    assign slv_aw_ready[lane] = dut.slv_if[lane].aw_ready;
    assign slv_w_ready[lane] = dut.slv_if[lane].w_ready;
    assign slv_ar_ready[lane] = dut.slv_if[lane].ar_ready;
    assign slv_b_valid[lane] = dut.slv_if[lane].b_valid;
    assign slv_b_resp[lane] = dut.slv_if[lane].b_resp;
    assign slv_r_valid[lane] = dut.slv_if[lane].r_valid;
    assign slv_r_resp[lane] = dut.slv_if[lane].r_resp;
    assign slv_r_data[lane] = dut.slv_if[lane].r_data;
  end

  function automatic logic [31:0] read_data(input logic [7:0] addr);
    return 32'hc0ff_ee00 | {24'b0, addr};
  endfunction

  function automatic logic [1:0] response(input int source);
    return source == 0 ? 2'b00 : 2'b10;
  endfunction

  // Each manager sends one write and one read. Each subordinate accepts one
  // outstanding transaction per channel; contention does not choose a winner.
  task automatic check_routes(input logic [7:0] addr0, addr1);
    bit [1:0] sent_aw, sent_w, sent_ar, saw_aw, saw_w, saw_ar, saw_b, saw_r;
    bit [1:0] pending_aw, pending_w, pending_ar;
    int aw_owner[2], ar_owner[2];
    bit finished;
    sent_aw = '0;
    sent_w = '0;
    sent_ar = '0;
    saw_aw = '0;
    saw_w = '0;
    saw_ar = '0;
    saw_b = '0;
    saw_r = '0;
    pending_aw = '0;
    pending_w = '0;
    pending_ar = '0;
    aw_owner = '{default: 0};
    ar_owner = '{default: 0};
    finished = 1'b0;
    rst_ni = 1'b0;
    aw_valid_i = '0;
    w_valid_i = '0;
    ar_valid_i = '0;
    b_valid_i = '0;
    r_valid_i = '0;
    repeat (4) @(negedge clk_i);
    aw_addr_i = {addr1, addr0};
    ar_addr_i = {addr1, addr0};
    aw_prot_i = {3'b101, 3'b001};
    ar_prot_i = {3'b011, 3'b010};
    w_data_i = {32'h5566_7788, 32'h1122_3344};
    w_strb_i = {4'ha, 4'h5};
    rst_ni = 1'b1;

    for (int cycle = 0; cycle < 100; cycle++) begin
      @(negedge clk_i);
      aw_valid_i = ~sent_aw;
      w_valid_i = ~sent_w;
      ar_valid_i = ~sent_ar;
      for (int target = 0; target < 2; target++) begin
        aw_ready_i[target] = !pending_aw[target] && (cycle + target) % 3 != 0;
        w_ready_i[target] = pending_aw[target] && !pending_w[target] && cycle % 3 != 1;
        ar_ready_i[target] = !pending_ar[target] && (cycle + target) % 3 != 1;
        b_valid_i[target] = pending_aw[target] && pending_w[target];
        r_valid_i[target] = pending_ar[target];
        b_resp_i[target] = response(aw_owner[target]);
        r_resp_i[target] = response(ar_owner[target]);
        r_data_i[target] = read_data(ar_addr_i[ar_owner[target]]);
        if ($test$plusargs("SVLENS_AXI_MUTATE_RESPONSE")) r_data_i[target] ^= 32'b1;
        b_ready_i[target] = cycle % 4 == target;
        r_ready_i[target] = cycle % 4 == 3 - target;
      end

      @(posedge clk_i);
      for (int target = 0; target < 2; target++) begin
        if (mst_aw_valid[target] && !aw_ready_i[target]) stalled_channels[0] = 1'b1;
        if (mst_w_valid[target] && !w_ready_i[target]) stalled_channels[1] = 1'b1;
        if (mst_ar_valid[target] && !ar_ready_i[target]) stalled_channels[2] = 1'b1;
        if (mst_aw_valid[target] && aw_ready_i[target]) begin
          int source;
          assert (mst_aw_addr[target] == addr0 || mst_aw_addr[target] == addr1)
            else $fatal(1, "unexpected AW address");
          source = mst_aw_addr[target] == addr0 ? 0 : 1;
          assert (int'(mst_aw_addr[target][7]) == target && !saw_aw[source] &&
                  mst_aw_prot[target] == aw_prot_i[source])
            else $fatal(1, "wrong or duplicate AW route");
          aw_owner[target] = source;
          pending_aw[target] = 1'b1;
          saw_aw[source] = 1'b1;
        end
        if (mst_w_valid[target] && w_ready_i[target]) begin
          int source;
          source = aw_owner[target];
          assert (pending_aw[target] && !saw_w[source] &&
                  mst_w_data[target] == w_data_i[source] &&
                  mst_w_strb[target] == w_strb_i[source])
            else $fatal(1, "wrong or duplicate W route");
          pending_w[target] = 1'b1;
          saw_w[source] = 1'b1;
        end
        if (mst_ar_valid[target] && ar_ready_i[target]) begin
          int source;
          assert (mst_ar_addr[target] == addr0 || mst_ar_addr[target] == addr1)
            else $fatal(1, "unexpected AR address");
          source = mst_ar_addr[target] == addr0 ? 0 : 1;
          assert (int'(mst_ar_addr[target][7]) == target && !saw_ar[source] &&
                  mst_ar_prot[target] == ar_prot_i[source])
            else $fatal(1, "wrong or duplicate AR route");
          ar_owner[target] = source;
          pending_ar[target] = 1'b1;
          saw_ar[source] = 1'b1;
        end
        if (b_valid_i[target] && mst_b_ready[target]) begin
          pending_aw[target] = 1'b0;
          pending_w[target] = 1'b0;
        end
        if (r_valid_i[target] && mst_r_ready[target]) pending_ar[target] = 1'b0;
      end
      for (int source = 0; source < 2; source++) begin
        if (slv_b_valid[source] && !b_ready_i[source]) stalled_channels[3] = 1'b1;
        if (slv_r_valid[source] && !r_ready_i[source]) stalled_channels[4] = 1'b1;
        if (aw_valid_i[source] && slv_aw_ready[source]) sent_aw[source] = 1'b1;
        if (w_valid_i[source] && slv_w_ready[source]) sent_w[source] = 1'b1;
        if (ar_valid_i[source] && slv_ar_ready[source]) sent_ar[source] = 1'b1;
        if (slv_b_valid[source] && b_ready_i[source]) begin
          assert (saw_aw[source] && saw_w[source] && !saw_b[source] &&
                  slv_b_resp[source] == response(source))
            else $fatal(1, "wrong or duplicate B response");
          saw_b[source] = 1'b1;
          total_writes++;
        end
        if (slv_r_valid[source] && r_ready_i[source]) begin
          assert (saw_ar[source] && !saw_r[source] &&
                  slv_r_data[source] == read_data(ar_addr_i[source]) &&
                  slv_r_resp[source] == response(source))
            else $fatal(1, "wrong or duplicate R response");
          saw_r[source] = 1'b1;
          total_reads++;
        end
      end
      if (&sent_aw && &sent_w && &sent_ar && &saw_aw && &saw_w && &saw_ar &&
          &saw_b && &saw_r && !(|pending_aw) && !(|pending_w) && !(|pending_ar)) begin
        finished = 1'b1;
        break;
      end
    end
    assert (finished) else $fatal(1, "AXI transaction timeout for %h/%h", addr0, addr1);
    @(negedge clk_i);
    aw_valid_i = '0;
    w_valid_i = '0;
    ar_valid_i = '0;
    b_valid_i = '0;
    r_valid_i = '0;
    aw_ready_i = '1;
    w_ready_i = '1;
    ar_ready_i = '1;
    b_ready_i = '1;
    r_ready_i = '1;
    repeat (4) begin
      @(posedge clk_i);
      assert (!(|mst_aw_valid) && !(|mst_w_valid) && !(|mst_ar_valid) &&
              !(|slv_b_valid) && !(|slv_r_valid))
        else $fatal(1, "extra transaction after completion");
    end
    @(negedge clk_i);
    passed_cases++;
  endtask

  initial begin
    aw_addr_i = '0;
    ar_addr_i = '0;
    aw_prot_i = '0;
    ar_prot_i = '0;
    w_data_i = '0;
    w_strb_i = '0;
    aw_valid_i = '0;
    ar_valid_i = '0;
    w_valid_i = '0;
    b_ready_i = '0;
    r_ready_i = '0;
    aw_ready_i = '0;
    w_ready_i = '0;
    ar_ready_i = '0;
    b_valid_i = '0;
    r_valid_i = '0;
    b_resp_i = '0;
    r_resp_i = '0;
    r_data_i = '0;
    check_routes(8'h10, 8'h90);
    check_routes(8'h90, 8'h10);
    check_routes(8'h00, 8'h80);
    check_routes(8'hff, 8'h7f);
    check_routes(8'h10, 8'h20);
    check_routes(8'h90, 8'ha0);
    assert (&stalled_channels) else $fatal(1, "missing channel backpressure: %b", stalled_channels);
    $display("PULP AXI 2x2 route simulation passed: %0d cases, %0d writes, %0d reads",
             passed_cases, total_writes, total_reads);
    $finish;
  end
endmodule
