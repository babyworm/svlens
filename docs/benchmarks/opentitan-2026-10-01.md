# OpenTitan benchmark snapshot — 2026-10-01

The source-only benchmark used OpenTitan `earlgrey_silver_release_v5` at
`ed044fc9760bdf9fc075d0015ba1db07fa075355` and svlens `0.3.6` from a
dirty worktree based on `5e30010958fab89b9bfdd648b57504ef361064c1`.
Analyzed binary SHA-256:
`a714b019c614e7a0e76e31c17ab573043a8441bf5f2687f199daf1086f96d988`.
`make bench` produced fresh conn and CDC JSON for all four targets. Times are
single-run macOS wall times and not cross-machine performance comparisons.

| Target | Conn rows (direct / approximate) | Bit-flow gaps | Required paths | Absent paths | CDC (violation / caution / info) | CDC signal references | Conn / CDC time |
|---|---:|---:|---:|---:|---:|---:|---:|
| aes | 622 (210 / 412) | 771 | 2/2 | 2/2 | 0 / 2 / 2 | — | 192 / 83 ms |
| hmac | 93 (46 / 47) | 213 | 1/1 | — | 0 / 0 / 0 | — | 90 / 45 ms |
| uart | 139 (56 / 83) | 175 | — | — | 0 / 0 / 0 | — | 81 / 45 ms |
| top_earlgrey | 18,995 (8,647 / 10,348) | 28,896 | 10/10 | 5/5 | 3 / 219 / 21 | 5/5, roots 5/5, category 1/1 | 1,905 / 1,015 ms |

Relative to the [2026-09-28 snapshot](opentitan-2026-09-28.md), runtime
element-select may-flow adds 494 range-free `approximate` SoC rows. Implicit
conversion mapping reduces `width_changing_conversion` gaps from 394 to 155;
`unresolved_destination_range` rises from 1,263 to 1,265, so total gaps fall
by 237. Exact connection rows, active issues, and CDC counts are unchanged. The
relationship matches a declaring scope and compatible fixed indices/member
names, but does not prove the selected lane or branch reachability. The
benchmark does not provide whole-SoC precision or recall.

Two new SoC references check the OTP response-valid path. In the pinned
`otp_ctrl.sv`, `u_otp.valid_o` and `u_otp_rsp_fifo.rvalid_o` feed the expression
assigned to `part_otp_rvalid[otp_part_idx]`; element zero reaches the
unbuffered partition's `otp_rvalid_i` (lines 673, 694, 702–704, 986).
Both possible sources appear as `approximate` connections (2/2). This checks
path presence, not that partition zero receives every response. The previous
six EDN bit-lane and two AES share-path references remain present, giving
10/10 bounded SoC connectivity references; four manually checked FIFO CDC
crossings and one power-manager cause-bus path retain their expected root
labels (5/5). The power-manager path has a synchronized change toggle controlling
capture, but this path-presence probe cannot prove source-data stability or
protocol safety. Its `VIOLATION` category is pinned as a regression check
(1/1), not an assertion that the RTL protocol is unsafe. The three reported
violations are real FF-to-FF data paths:
two SPI pass-through paths between mode-selected SCK phases and this
power-manager path. Their clock relationship or data-hold requirements need
further timing/protocol review; the benchmark does not label them safe or
unsafe. The wide-path recommendations now call for a coherent multi-bit CDC
scheme, not separate 2-FF synchronizers for each bit. Topology-only
domain-pair probes remain 0/6 in local labels and 2/6 in root provenance;
absent pairs are not established signal-level false negatives.

The benchmark also rejects five manually selected SoC false-path candidates:
two swapped AES data/mask shares, two EDN0/EDN1 consumer misroutes, and the
OTP response FIFO's `rvalid_o` to its `rready_i`. The AES-only report checks
the two share swaps. Both ends of every absent-path probe occur independently
in other connection rows, so a missing port cannot satisfy it vacuously.
All five SoC and both AES probes are absent. This is a small, selected
negative sample—not a whole-SoC false-positive rate or precision estimate.
This run provides no SDC, so its unchanged CDC counts do not exercise the
`set_false_path` or exclusive-clock-group classification corrections. Separate
fixture tests prove that a clock-to-clock false path does not downgrade an
unsynchronized crossing, exclusive groups retain a CAUTION review item, and
the generated SDC output contains only review comments. A separate SDC
fixture uses static `[get_clocks {name}]` selectors to verify that exclusive
relationships are registered. Another parses `-include_generated_clocks` with
a literal root and verifies SDC-declared relations through a forward-declared
generated-clock master chain. Other Tcl options and wildcard selectors remain
unsupported and are skipped as whole group commands with a CLI warning;
unresolved or overlapping groups are also skipped rather than partially bound.
The new `set_max_delay` report fields are likewise not exercised by this
no-SDC benchmark; fixture tests verify they remain declared timing context
without changing a CDC violation.

Raw reports and logs are under ignored `bench/opentitan/results/`; the
scheduled/on-demand benchmark workflow uploads them as an artifact. Generic
primitive substitution and source-only elaboration remain limitations of
this benchmark.
