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
paths remain present. At that point it was not a committed CI gate and did not
establish correctness of internal arbitration, mux selection, or all AXI
channels.

These counts are observed analyzer output, not precision or recall estimates
for the full SoC. The 20 selected OpenTitan rows and four PULP address paths
cannot certify the thousands of other newly added rows; other dynamic
packed-array shapes and complex procedural glue remain incomplete.

## Follow-up: bounded runtime packed-field reads

A runtime index into a fixed packed-struct array now enumerates each possible
field as a positional `approximate` may-flow source; its selector is recorded
as a separate range-free control dependency. The regression confirms both
candidate `data` and nonzero-offset `valid` fields without cross-field links
or a fabricated `direct` lane.

On the same pinned OpenTitan source, the KMAC `app_i[app_id]` read contributes
20 new `approximate/ranged` rows and removes 16 `nonstructural_source` gap
instances. The current totals are 31,717 connections (20,135 direct, 11,582
approximate) and 26,179 recorded gaps. RTL packs `app_req_t` as `valid` at
bit 73 and `data` at bits 72:9 (`kmac_pkg.sv:185–192`); `kmac_app.sv:443–453`
uses the runtime `app_id` to select these fields. Two exact-range positive
references pin one possible KeyMgr source to KMAC's FIFO `data` and `valid`
inputs, raising SoC positive gates to 22/22. They do not claim KeyMgr is the
active app in a given cycle. The 20 sampled IDs are unchanged, but their
population hash is now
`0adfd06f1ca9850d8e28f70e04e8ededbcf70411b4ecd41ab2461bfb9baca038`.

## Follow-up: runtime bare packed vectors

The same bounded candidate logic now handles an assignment-RHS runtime
element read from a bare packed vector. A test with two independently driven
byte lanes requires both positional `approximate` data candidates and a
separate range-free selector dependency. Constant-indexed vectors retain
their earlier `direct` paths; the OpenTitan alert-handler `class_trig_o[0]` →
`gen_classes[0].u_accu.class_trig_i` lane is pinned as a regression guard.

Relative to the previous committed artifact (`cc8722a`), OpenTitan gains 37
`approximate/ranged` rows and removes none, for 31,754 connections (20,135
direct, 11,619 approximate). Recorded bit-flow gaps fall from 26,179 to
25,841. The new candidates include Flash information-memory data and metadata
selected by `info_sel_q` (`prim_generic_flash_bank.sv:403–445`), a USB endpoint
setup bit selected by `out_endpoint_o` for receive-FIFO bit 12
(`usbdev_usbif.sv:188,194–199`), and a debug-module halted-status bit.
Positive path gates are 25/25, negative gates remain 7/7, and the 20 sampled
IDs retain their source-backed verdicts. Their population SHA-256 is now
`922645f3d66866a6a9d4b141b8e7c4be03b576f6c88bde6dd250e93a12d55a59`.
These are possible combinational sources, not proof of a chosen runtime index
or a full-design precision/recall measurement.

The optional pinned PULP AXI 2×2 probe now reports 488 connection rows and
140 bit-flow gaps. Its earlier 76/76 interface-boundary rows and 4/4
manager-to-decoder AW/AR paths remain present, with no opposite-lane address
path. New approximate ranged rows include possible response paths from either
master mux to either slave demux, as expected for a fully connected crossbar;
the individual arbitration outcomes and these new rows have not been
independently adjudicated or added to CI.

## Follow-up: repeatable PULP AXI gate

The repository now contains the fixture, pinned-clone runner, and an
84-relation evaluator under `bench/pulp_axi/`. On manual dispatch, the
existing benchmark workflow runs the PULP probe as a separate job and uploads
its report; scheduled OpenTitan runs do not fetch PULP sources. The frame
requires all 76 directed modport boundary paths with full bit ranges, the
four same-lane AW/AR decoder paths, and four positional `approximate`
master-response to slave-demux candidates. It rejects missing, duplicate,
downgraded, range-changed, reversed, and wrong-lane boundary or decoder paths.
The expected-pair SHA-256 is
`f89dc8640861f95e1f437724635e5a2777f231695a09eb09ca0b2660737a9730`.
The local pinned run reports 488 connection rows and 140 bit-flow gaps.
The rest of those rows are not source-adjudicated by this frame, so the gate
still does not measure overall AXI accuracy or arbitration correctness.

## Follow-up: packed selection in an input-port expression

An ordinary input port connected directly to a runtime packed-array select
now receives the same bounded positional candidate ranges and a separate
range-free index-control relation. Small fixtures require both candidate
byte lanes and the selector, including separate `data`/`valid` fields of a
packed struct; the older nested packed-array port fixture now checks its
three approximate byte ranges. No runtime candidate is promoted to `direct`.

The pinned OpenTitan report gains one coarse relation from KeyMgr
`u_ctrl.stage_sel_o` to `u_kmac_if.id_data_i`, consistent with
`keymgr.sv:341–352,410–412`. It has 31,755 connection rows and the same
25,841 recorded gaps. A new positive reference brings path gates to 26/26;
the 20 selected sample IDs retain their verdicts under population hash
`b0534e8970c1b2c420c7f6846fbdb1063195d79bcc7e781f265cb77e05b001d6`.
This does not prove which KeyMgr stage is active. Wider expressions and
dynamic interface-instance selections still lack this candidate guarantee.

## Follow-up: whole packed-struct port to field flow

Mapping a plain packed-struct field to its declaring aggregate's bit offset
lets a whole-struct input port meet internal field reads and writes. A small
fixture checks `data` and nonzero-offset `valid` independently across the
module boundary. In the pinned PULP AXI-Lite 2×2 design this recovers all
24 manager-to-subordinate AW address/protection, W data/strobe, and AR
address/protection candidate paths (six signals × four possible pairs). They
are positional `approximate` paths because demux selection and master
arbitration are runtime decisions. The optional benchmark now gates 108
relations, with expected-pair SHA-256
`90ba4b21381fcb611dbe33d20d81ba3ce8440098b98362b8aa699849b73157ad`.
Its local report has 1,030 connection rows and 140 recorded gaps; the earlier
76 boundary, four decoder, and four response checks still pass.

OpenTitan connection rows rise from 31,755 to 97,002 (64,841 direct, 32,161
approximate), while recorded gaps remain 25,841. Eight new rows entered the
deterministic sample and were checked against pinned RTL: the Domain0 system
reset mux to CSRNG; the IO-div4 peripheral clock gate to ADC; Ibex
`last_data_addr[6]` at buffer bit 39 (`1 + 32 + 6`); TL `a_data[24]` at
aggregate bit 46 (`1 + 21 + 24`) to a PLIC subregister; and four possible
reset routes through scan-mode muxes. Four selected relations are also new
exact path/bit references. The 20-row sample is 20/20 confirmed, positive
references are 30/30, negative probes remain 7/7, and the population hash is
`efd44aae9e800f649358e6f7658bb90feac7b5d511ff00964a548797d3b1ce4b`.
The large remaining population is not individually source-adjudicated; these
counts must not be read as a whole-SoC precision or recall estimate.

## Follow-up: AXI fixture reachability

The first 2×2 fixture drove both managers' AW/AR addresses to `8'h10`, so its
report's manager-to-subordinate-1 candidate paths were structural may-flow
but could not be selected by those fixed inputs. The committed fixture now
exposes independent runtime AW/AR addresses, request data, and handshake
inputs per manager, along with independent subordinate responses. Both
address-map regions (`0–127` and `128–255`) are therefore selectable from
either manager. The runner pins the fixture SHA-256
`078ed80e9a7bf7a7e2fae6b67bfb3bf0b6fa740db9c0a933abb1b18597a49e70`.
The connection-row population and 108/108 frame are unchanged. These inputs
make the candidate routes reachable in principle; the benchmark still does
not by itself verify which arbitration choice occurs.

## Follow-up: two transaction-level route checks

The optional existing-Verilator smoke now drives two runtime address cases:
manager 0→subordinate 0 and manager 1→subordinate 1, then the assignments are
swapped. Distinct AW/AR addresses and W data are checked at subordinate
ports; distinct B responses and R data are checked on return to their owning
managers. All five channels must be observed on both targets in each case.
The first simulation attempt exposed a fixture error: it paired 8-bit AXI
addresses with a 64-bit address-rule type, violating PULP's own decoder
assertion. A pinned 8-bit rule type now uses the upstream `end_addr=0`
sentinel for the upper end of the address space. The structural 108-pair frame
and 1,030-row population remain unchanged. Fixture SHA-256 is
`c626f223ab39988be03e638e79276e604e5e7fc4654dcfd69956781531b106af`;
the testbench SHA-256 is
`5a7b694f58dbd264765e34bb4d5834a5f7bb8375b900882bfef00e4d4d379bbd`.
This is a two-case behavioral smoke, not exhaustive arbitration or
backpressure verification.
