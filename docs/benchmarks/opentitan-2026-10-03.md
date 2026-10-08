# OpenTitan benchmark snapshot — 2026-10-03

The source-only benchmark used OpenTitan `earlgrey_silver_release_v5` at
`ed044fc9760bdf9fc075d0015ba1db07fa075355` and svlens `0.3.6` from a
dirty worktree based on `5e30010958fab89b9bfdd648b57504ef361064c1`.
Analyzed binary SHA-256:
`c7a75148a2451a74f8423c66ac025fafea732bcdac16b954e97cf06ba0d7cb4a`.
`make bench` produced fresh conn and CDC JSON for all four targets, plus a
separate period-context CDC JSON for `top_earlgrey` and separate CDC/SVA
pairs for AES and the SoC. Times are
single-run macOS wall times, not cross-machine performance comparisons.

| Target | Conn rows (direct / approximate) | Bit-flow gaps | Required paths | Absent paths | CDC (violation / caution / info) | CDC signal references | Conn / CDC time |
|---|---:|---:|---:|---:|---:|---:|---:|
| aes | 642 (210 / 432) | 759 | 2/2 | 2/2 | 0 / 2 / 2 | — | 128 / 77 ms |
| hmac | 95 (46 / 49) | 213 | 3/3 | 2/2 | 0 / 0 / 0 | — | 39 / 40 ms |
| uart | 139 (56 / 83) | 174 | — | — | 0 / 0 / 0 | — | 40 / 41 ms |
| top_earlgrey | 19,357 (8,647 / 10,710) | 27,985 | 11/11 | 5/5 | 3 / 219 / 21 | 5/5, roots 5/5, category 1/1 | 1,576 / 994 ms |

Relative to the [2026-10-02 snapshot](opentitan-2026-10-02.md), the previously
pinned path, root, category, and CDC results are unchanged; one further
connectivity path is now pinned, and row/gap totals changed as detailed below. `Ac_cdc09` now
reports a scope-resolved clock-net path rather than a leaf name. A fixture
checks that a top clock connected to a differently named child data port
appears under that child port's path in JSON. The pinned OpenTitan references
do not independently measure this rule's accuracy. The new whole-interface
slice fixtures prove lower/upper/ascending overlap, modport-to-whole-interface
pairing, two direct interface-to-interface rewiring stages, isolation from an
unrelated interface chain, and range-limited computed/guard reads that remain
approximate. The equal-width ternary change adds 287 approximate SoC rows
and reduces recorded bit-flow gaps by 779 while direct rows, selected path
references, and CDC counts remain unchanged. These new candidates have not
been manually labeled and are not evidence of SoC-wide precision, recall, or
interface-slice coverage.
The constant-ternary fixture also rejects the unselected interface arm at
its local input port; this pinned benchmark does not independently label that
case.
The latest conversion pass maps simple explicit size casts and unsigned
concatenations inside size casts, and removes range-free `direct` aliases that
contradicted width- or state-changing bit flows. Relative to the immediately
preceding local run (19,282 SoC rows; 28,117 gaps), direct SoC rows stay at
8,647, approximate rows fall by 9, and bit-flow gaps fall by 113;
the new `state_changing_conversion` reason accounts for 314 SoC gaps.
AES gains five approximate rows and 33 gaps, while UART loses two approximate
rows and gains one gap. These are analyzer-output changes, not measured
precision or recall; the affected SoC paths have not been independently labeled.
The sole-assignment `always_comb` exact-lane fixtures add no rows or gap changes
in this pinned benchmark; they establish only those local positive and negative
patterns, not general procedural-glue accuracy.
Before constant-branch pruning, the guarded-copy pass added 92 range-bearing `approximate` SoC rows and five AES
rows relative to the immediately prior local run. Comparing full SoC connection
rows with that run found zero missing old rows. A new 11th reference pins the
AES sparse-signal `crypt_q` feedback into `crypt_d`, which would have been lost
if the coarse dependency had been removed before a later cast. These are
coverage and regression checks, not SoC-wide accuracy estimates.
Compile-time-known `if` and ordinary `case` pruning then removes eight
approximate SoC rows and 19 recorded bit-flow gaps, while all 11 positive and
five absent-path probes still pass. All eight removed rows linked Ibex decoder
outputs to `controller_i.branch_not_set_i` in two core instances. The pinned
top sets `BranchPredictor=0` (`top_earlgrey.sv:760`), and the only conditional
assignment to `branch_not_set` is inside `if (BranchPredictor)`
(`ibex_id_stage.sv:768,805–807`); this supports classifying those eight rows as
false positives. It is a bounded RTL audit, not a whole-SoC precision result.
Constant `casez`/`casex` and computed-`if` fixtures now reject inactive
inputs while runtime wildcard-case fixtures retain both possible sources.
This change did not alter the pinned SoC connection JSON (identical SHA-256
before and after); SoC-wide wildcard-case coverage remains unmeasured.

The HMAC source now gates two additional `approximate` FIFO mux influences:
`u_sha2.digest` can supply a runtime-selected data word, while
`u_hmac.fifo_wdata_sel` controls that word selection
(`hmac.sv:267–281,401–433`). Neither implies a fixed bit mapping or that
every branch is reached. Paired negative probes reject either source as a
driver of the core's FIFO-ready input; `prim_fifo_sync.sv:83–95` computes ready
from occupancy and reset. These are selected RTL checks (3/3 positive,
2/2 absent), not HMAC-wide precision or recall.
The one-bit inverted-reset fixtures identify a unique registered reset driver
through `~` or `!`, a directed module-port chain, or one unconditional
`always_comb` assignment. Conditional and overwritten assignments stay
unresolved; the recommendation asks for deassertion review without declaring
a reset synchronizer absent. OpenTitan's CDC counts are unchanged; this
benchmark does not independently validate the inverted-reset rule or a
complete RDC tree.
An additional fixture uses `~u_source.reset_q`; it resolves that exact child
FF despite a sibling with the same `reset_q` leaf. This benchmark does not
independently exercise that form either.
The reset-use inventory contains 7,473 distinct paths, but none resolves to
a unique FF driver in this source-only SoC run. Direct, odd-, and even-inversion
provenance is verified in fixtures, not by this SoC sample; zero identified
drivers does not establish that no RDC risk exists.
Scalar packed-struct reset fields and clocked member FF reads/writes are now
covered by separate fixtures, but the pinned SoC counts remain unchanged;
indexed reset-array branches and reset-manager combinational logic still need
independent lineage analysis.
An additional fixture now traces one fixed-selected packed reset bit to its
unique aggregate FF; a dynamic selector and duplicate FF path remain
unresolved. In the pinned SoC, `rstmgr.sv:390–414` selects the synchronized
reset or `scan_rst_ni` through `prim_clock_mux2` before HMAC receives it
(`top_earlgrey.sv:2166–2168`). This nontransparent path is pinned as one
observed-but-unresolved reset reference (1/1), not an inferred FF driver.
A new paired-bit fixture traces two separately registered scalar outputs into
different constant-selected bits of one reset vector. The indexed edges are
reset-only; a dynamic selector remains unresolved. The subsequent conditional
mux topology pass finds candidates on 5,319 of 7,473 SoC reset-use paths,
across 29 distinct mux outputs, with no traversal truncated. The HMAC reset
reference (1/1) now pins `u_0_sys_mux`'s exact output, two inputs, and selector,
plus `rst_sys_n[1]` and `scan_rst_ni` as immediately connected input nets, while
requiring `driver_ff` to remain absent. Its input-0 branch reaches candidate
FF `u_0_sys.u_sync_2.q_o` in `clk_main_i` with non-inverted input-relative
polarity; the scan-reset input has no
registered FF candidate in this source-only graph. This branch evidence is
not a runtime mux-output driver verdict.
The select expression depends on `leaf_rst_scanmode[7]`; it is not mislabeled
as a direct selector source. A width-changing cast or computed input remains
a may-dependency rather than a transparent clock/reset alias. The full SoC
crossing array was byte-identical before and after that cast correction.
Across the 29 mux outputs, input 0 has 26 direct connected nets and three
computed-expression dependency sets; selectors have five direct nets and 24
dependency sets. These are expression-shape counts, not branch or reset-safety
validation. Fixed-0, fixed-1, and X-selector fixtures verify that only a
known branch that independently reaches a unique FF supplies driver provenance;
an unrelated alias cannot substitute for an external selected branch. A cycle
also remains unresolved. None of the pinned SoC's 29 mux
outputs has a fixed selector. None of the 5,319 mux-bearing reset records
claims a unique FF driver; the benchmark now gates that count
at zero. Of those reset paths, 5,288 show at least one branch FF candidate,
spanning 21 distinct FFs; no trace was truncated. A separate fixture verifies
that a direct indexed struct-field route
can still resolve its scalar FF. CDC classifications stay at
3 / 219 / 21. These are structural candidates, not selected scan modes,
proven synchronizers, or a complete reset tree.
Further fixtures trace scalar `~`/`!` inversions at mux inputs without
merging clock domains. A fixed selector retains odd inversion parity on its
registered reset source, while a runtime selector reports the FF only as a
branch candidate and a wider inversion stays unresolved. The pinned SoC
crossing array and mux candidate counts did not change.
Candidate-FF entries now distinguish direct from inverted paths; a synthetic
reconvergence with both polarities omits a chosen parity and marks ambiguity.
This does not certify reset polarity or timing through a runtime mux.

The five CDC signal references check four FIFO paths and the power-manager
cause-bus path. The latter's current `VIOLATION` classification is pinned,
but its bundled-data hold interval remains unverified. Two SPI mode-selected
clock paths also remain reported as violations without adjudicated timing
relationships. The primary CDC run has no SDC; the supplemental projection
below supplies only four base-clock periods. These selected probes are not whole-SoC
precision or recall estimates. Raw logs and reports remain under ignored
`bench/opentitan/results/`; the scheduled/on-demand workflow uploads them.
Guidance probes for both SPI paths and the power-manager bus pass 3/3: they
retain `VIOLATION` and avoid an unconditional per-bit 2FF instruction. This
checks report wording, not the safety of any existing transfer protocol.

## CDC period-context probe

[`top_earlgrey_periods.sdc`](../../bench/opentitan/constraints/top_earlgrey_periods.sdc)
projects ASIC synthesis periods onto the four `top_earlgrey` base-clock ports
connected to `ast_base_clks` in pinned `chip_earlgrey_asic.sv:1088–1091`.
It omits generated clocks, clock groups, modes, I/O delays, and exceptions;
its SHA-256 is
`93eeb7fd359d078f19b6f9486fda0a0ef047b7804db691bb51e87baf9fed5a15`.
The separate run took 1,031 ms. All four base-clock domain periods match their
projected values (4/4); other domain periods remain `null` unless separately
derived. It populated `timing_basis_ns` on 157/243
crossings, all with value 4750 ns from the projected AON period. The
power-manager bundled-data path has the expected AON_CLK → IO_CLK roots and
4750 ns basis (1/1). Every path kept its structural category, severity,
rule, and synchronization type. This does not establish a period for its
divided IO capture clock, data hold interval, phase, or physical delay.

## CDC SVA contract probe

The separate AES `cdc_sva` run took 78 ms and kept the primary run's four
crossing classifications unchanged. Its `prim_sync_reqack_data` instance in
`aes.sv:107–111` has `DataSrc2Dst=0` and `DataReg=0`. The evaluator matches
the `dst_ack_i` → `src_ack_o` CAUTION crossing to its JSON
`sva_assertion_id`, the generated `_data_hold_dst2src` property, and the
two-cycle `$past(data_o, 2)` source-clock window (1/1). The emitted SVA
compiled beside the pinned AES RTL with `slang` (0 errors, 0 warnings).
This checks property emission and elaboration, not simulation results,
data stability in the actual integration, or CDC protocol safety.

The separate SoC `cdc_sva` run took 1,049 ms. It emits 320 assert labels,
all linked exactly once from the JSON crossing IDs (320/320), while retaining
all 243 primary crossing classifications. Four pinned references pass (4/4):
the USB device async FIFO's 2FF transfer and its secondary Gray-pointer
transition assertion on the same crossing, the SPI host req/ack ACK contract,
and the SPI host command's source-to-destination data-hold contract. The SoC
SVA compiled beside the pinned RTL with no errors. Its 12 implicit-conversion
warnings also occur when compiling the RTL without SVA. Neither compilation
nor label matching simulates the properties or proves FIFO/handshake safety.
