# Packed-struct array forwarding snapshot — 2026-10-17

The analyzer now flattens a constant-selected field of a packed-struct array
element to its ordinal bit interval in the aggregate vector. It carries that
interval through an ordinary module input, including reversed packed-array
index directions. A runtime-selected element does not acquire a direct lane.
The local regression checks four data and four nonzero-offset `valid` paths,
their wrong-lane absences, a dynamic-index negative case, and two nested
`aw.addr` decoder paths with opposite-lane negatives.

Pinned OpenTitan `earlgrey_silver_release_v5` at
`ed044fc9760bdf9fc075d0015ba1db07fa075355` was run with `make bench`.
The preceding committed report (`41e8e58`) is the comparison baseline.

| top_earlgrey | Before | After |
|---|---:|---:|
| Connection rows | 24,095 | 31,697 |
| Direct / approximate | 13,407 / 10,688 | 20,135 / 11,562 |
| Recorded bit-flow gaps | 27,175 | 26,195 |
| Positive / negative path gates | 17/17 / 7/7 | 20/20 / 7/7 |

The three new positive gates pin EDN register write-data bit 1, the full
107-bit xbar-to-KMAC TL request, and ROM scrambled-data bit 23 to flattened
`hw2reg.exp_digest[2].d[23]` at bit 90. The last offset follows the RTL
packing: eight 33-bit EXP_DIGEST elements start at bit 0; element 2's `d`
field begins one bit above `de`, so `33×2 + 1 + 23 = 90`.
Six new rows entered the deterministic 20-row sample. Source inspection
confirmed the ROM mapping above, EDN register bit 1, PLIC gateway bit 136,
the KMAC TL request, flash register write-data bit 0, and a conditional
key-manager LFSR data arm. The latter remains `approximate`: LFSR bit 1 can
feed `hw2reg.sw_share0_output[5].d[1]` at packed bit
`279 + 33×5 + 1 + 1 = 446` when its runtime selector chooses `data_rand[0]`.
The current population SHA-256 is
`d5635d288179daf18cc5756b5fb2a97a903bbc12b56bc980976f71d2cbb7286a`;
all 20 selected rows are confirmed, with no contradiction or unresolved label.
The independent source frames remain 358/358 direct wiring, 16/16 module
pass-through, and 6/6 ternary control.

A separate one-off 2×2 AXI-Lite probe used PULP AXI
`70b8e54fd460e3308e58be596ceb3566a6e3576e` and common_cells
`e73baaec2ca665cd80c3c384e9258e35242b829c`, without adding either as
a repository dependency. Its connection rows rose from 168 to 388 and
recorded bit-flow gaps fell from 304 to 166. The four manager AW/AR address
paths to the corresponding internal `cc_addr_decode.addr_i[7:0]` are direct
with source and destination bits `[7:0]`; no manager address was linked to
the opposite lane's decoder. The earlier 76/76 external modport boundary
paths remain present. This probe is not a committed CI gate and does not
establish correctness of internal arbitration, mux selection, or all AXI
channels.

These counts are observed analyzer output, not precision or recall estimates
for the full SoC. The 20 selected OpenTitan rows and four PULP address paths
cannot certify the thousands of other newly added rows; dynamic packed-array
reads and complex procedural glue remain incomplete.
