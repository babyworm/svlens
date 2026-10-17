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

  logic [1:0] mst_aw_valid, mst_ar_valid, mst_w_valid;
  logic [1:0] slv_b_valid, slv_r_valid;
  logic [1:0][7:0] mst_aw_addr, mst_ar_addr;
  logic [1:0][31:0] mst_w_data;
  logic [1:0][1:0] slv_b_resp, slv_r_resp;
  logic [1:0][31:0] slv_r_data;
  assign mst_aw_valid[0] = dut.mst_if[0].aw_valid;
  assign mst_aw_valid[1] = dut.mst_if[1].aw_valid;
  assign mst_ar_valid[0] = dut.mst_if[0].ar_valid;
  assign mst_ar_valid[1] = dut.mst_if[1].ar_valid;
  assign mst_w_valid[0] = dut.mst_if[0].w_valid;
  assign mst_w_valid[1] = dut.mst_if[1].w_valid;
  assign mst_aw_addr[0] = dut.mst_if[0].aw_addr;
  assign mst_aw_addr[1] = dut.mst_if[1].aw_addr;
  assign mst_ar_addr[0] = dut.mst_if[0].ar_addr;
  assign mst_ar_addr[1] = dut.mst_if[1].ar_addr;
  assign mst_w_data[0] = dut.mst_if[0].w_data;
  assign mst_w_data[1] = dut.mst_if[1].w_data;
  assign slv_b_valid[0] = dut.slv_if[0].b_valid;
  assign slv_b_valid[1] = dut.slv_if[1].b_valid;
  assign slv_r_valid[0] = dut.slv_if[0].r_valid;
  assign slv_r_valid[1] = dut.slv_if[1].r_valid;
  assign slv_b_resp[0] = dut.slv_if[0].b_resp;
  assign slv_b_resp[1] = dut.slv_if[1].b_resp;
  assign slv_r_resp[0] = dut.slv_if[0].r_resp;
  assign slv_r_resp[1] = dut.slv_if[1].r_resp;
  assign slv_r_data[0] = dut.slv_if[0].r_data;
  assign slv_r_data[1] = dut.slv_if[1].r_data;

  task automatic check_route(input logic [7:0] addr0, addr1,
                             input logic [31:0] data0, data1,
                             input logic [1:0] src_for_target);
    bit [1:0] saw_aw, saw_ar, saw_w, saw_b, saw_r;
    rst_ni = 1'b0;
    aw_valid_i = '0;
    ar_valid_i = '0;
    w_valid_i = '0;
    b_valid_i = '0;
    r_valid_i = '0;
    repeat (4) @(negedge clk_i);
    aw_addr_i = {addr1, addr0};
    ar_addr_i = {addr1, addr0};
    w_data_i = {data1, data0};
    b_resp_i = {2'b10, 2'b01};
    r_resp_i = {2'b11, 2'b01};
    r_data_i = {32'hbad1_0001, 32'hbad0_0000};
    aw_valid_i = '1;
    ar_valid_i = '1;
    w_valid_i = '1;
    rst_ni = 1'b1;
    for (int cycle = 0; cycle < 20; cycle++) begin
      @(negedge clk_i);
      if (cycle == 3) begin
        b_valid_i = '1;
        r_valid_i = '1;
      end
      for (int target = 0; target < 2; target++) begin
        logic [7:0] expected_addr;
        logic [31:0] expected_data;
        expected_addr = src_for_target[target] ? addr1 : addr0;
        expected_data = src_for_target[target] ? data1 : data0;
        if (mst_aw_valid[target]) begin
          assert (mst_aw_addr[target] == expected_addr)
            else $fatal(1, "AW lane %0d received %h, expected %h", target,
                        mst_aw_addr[target], expected_addr);
          saw_aw[target] = 1'b1;
        end
        if (mst_ar_valid[target]) begin
          assert (mst_ar_addr[target] == expected_addr)
            else $fatal(1, "AR lane %0d received %h, expected %h", target,
                        mst_ar_addr[target], expected_addr);
          saw_ar[target] = 1'b1;
        end
        if (mst_w_valid[target]) begin
          assert (mst_w_data[target] == expected_data)
            else $fatal(1, "W lane %0d received %h, expected %h", target,
                        mst_w_data[target], expected_data);
          saw_w[target] = 1'b1;
        end
        if (slv_b_valid[src_for_target[target]]) begin
          assert (slv_b_resp[src_for_target[target]] == b_resp_i[target])
            else $fatal(1, "B manager %0d received wrong response", src_for_target[target]);
          saw_b[src_for_target[target]] = 1'b1;
        end
        if (slv_r_valid[src_for_target[target]]) begin
          assert (slv_r_resp[src_for_target[target]] == r_resp_i[target] &&
                  slv_r_data[src_for_target[target]] == r_data_i[target])
            else $fatal(1, "R manager %0d received wrong response", src_for_target[target]);
          saw_r[src_for_target[target]] = 1'b1;
        end
      end
    end
    assert (&saw_aw && &saw_ar && &saw_w && &saw_b && &saw_r)
      else $fatal(1, "missing route AW=%b AR=%b W=%b B=%b R=%b", saw_aw, saw_ar, saw_w, saw_b, saw_r);
  endtask

  initial begin
    aw_addr_i = '0;
    ar_addr_i = '0;
    aw_prot_i = '0;
    ar_prot_i = '0;
    w_data_i = '0;
    w_strb_i = '1;
    aw_valid_i = '0;
    ar_valid_i = '0;
    w_valid_i = '0;
    b_ready_i = '1;
    r_ready_i = '1;
    aw_ready_i = '1;
    w_ready_i = '1;
    ar_ready_i = '1;
    b_valid_i = '0;
    r_valid_i = '0;
    b_resp_i = '0;
    r_resp_i = '0;
    r_data_i = '0;
    check_route(8'h10, 8'h90, 32'h1122_3344, 32'h5566_7788, 2'b10);
    check_route(8'h90, 8'h10, 32'h99aa_bbcc, 32'hddee_ff00, 2'b01);
    $display("PULP AXI 2x2 route simulation passed");
    $finish;
  end
endmodule
