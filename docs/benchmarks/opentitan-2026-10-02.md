# OpenTitan benchmark snapshot — 2026-10-02

The source-only benchmark used OpenTitan `earlgrey_silver_release_v5` at
`ed044fc9760bdf9fc075d0015ba1db07fa075355` and svlens `0.3.6` from a
dirty worktree based on `5e30010958fab89b9bfdd648b57504ef361064c1`.
Analyzed binary SHA-256:
`b7fc0fb9e612051f4553f4f6e1728936ef1b4cbf9b3fb8cb77716338257d1395`.
`make bench` produced fresh conn and CDC JSON for all four targets. Times are
single-run macOS wall times, not cross-machine performance comparisons.

| Target | Conn rows (direct / approximate) | Bit-flow gaps | Required paths | Absent paths | CDC (violation / caution / info) | CDC signal references | Conn / CDC time |
|---|---:|---:|---:|---:|---:|---:|---:|
| aes | 622 (210 / 412) | 771 | 2/2 | 2/2 | 0 / 2 / 2 | — | 87 / 82 ms |
| hmac | 93 (46 / 47) | 213 | 1/1 | — | 0 / 0 / 0 | — | 48 / 44 ms |
| uart | 139 (56 / 83) | 175 | — | — | 0 / 0 / 0 | — | 48 / 39 ms |
| top_earlgrey | 18,995 (8,647 / 10,348) | 28,896 | 10/10 | 5/5 | 3 / 219 / 21 | 5/5, roots 5/5, category 1/1 | 1,423 / 932 ms |

Compared with the [2026-10-01 snapshot](opentitan-2026-10-01.md), bounded
path, root, category, gap, and CDC counts are unchanged. The new regression
prevents `Ac_cdc10` from treating an unrelated same-named FF as an inferred
clock driver while retaining a driver reached through directed port aliases.
It also keeps sibling instances' internal data clocks in separate domains.
This pinned workload does not exercise these edge cases. A
top-level data-named input used as a clock remains a naming hint, not proof
of an internal data-as-clock violation.

The five CDC signal references check four FIFO paths and the power-manager
cause-bus path. The latter's current `VIOLATION` classification is pinned;
its bundled-data hold interval remains unverified. The category check is a
regression gate, not a verdict that the RTL protocol is unsafe. Two SPI
mode-selected clock paths are also reported as violations, but their timing
relationship has not been adjudicated. No SDC was supplied for this benchmark. These
selected references are not whole-SoC precision or recall estimates.

Raw reports and logs are under ignored `bench/opentitan/results/`; the
scheduled/on-demand workflow uploads them as an artifact. Generic primitive
substitution and source-only elaboration remain benchmark limitations.
