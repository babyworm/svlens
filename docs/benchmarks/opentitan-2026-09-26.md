# OpenTitan benchmark snapshot — 2026-09-26

OpenTitan tag `earlgrey_silver_release_v5` at commit
`ed044fc9760bdf9fc075d0015ba1db07fa075355`; svlens `0.3.6` based on
commit `5e30010958fab89b9bfdd648b57504ef361064c1` with an uncommitted
worktree. The analyzed binary has SHA-256
`f1572fb35015f30ce7a6b64c078c64604f84dbca0974348ce9abf8fcfe06dd07`.
The local run used `bench/opentitan/run.py` and `evaluate.py` on
macOS. Times are single-run wall times and should not be compared across
machines. Each run used the selected FuseSoC default RTL filesets,
`SYNTHESIS`, `--single-unit`, and benchmark-local generic primitive aliases.
The earlier snapshot from this date was discarded: its source lists omitted
dependencies, and semantic elaboration errors were not yet checked. All four
source lists in this snapshot passed independent slang elaboration with zero
errors (AES, HMAC, UART: zero warnings; top_earlgrey: 12 warnings).

| Target | Conn edges | Conn active issues | CDC violations | CDC cautions | Known domain pairs observed | Conn / CDC time |
|---|---:|---:|---:|---:|---:|---:|
| aes | 481 | 100 | 0 | 2 | 2 / 2 | 78 / 78 ms |
| hmac | 41 | 117 | 0 | 0 | no labeled pairs | 40 / 40 ms |
| uart | 54 | 78 | 0 | 0 | no labeled pairs | 39 / 41 ms |
| top_earlgrey | 7,638 | 8,464 | 12 | 246 | 0 / 6 | 1,220 / 3,998 ms |

All four targets produced conn and CDC JSON reports. The conn issues in this
run are `DANGLING_OUTPUT`; OpenTitan intentionally leaves some output ports
open, so the counts are not a false-positive rate. The golden files identify
clock-domain pairs, not individual crossings. The AES 2/2 result comes from
CAUTION records for a named `prim_sync_reqack_data` port signature; it does
not verify the primitive's internal synchronization. The SoC 0/6 means its
top-level domain labels did not occur together in emitted crossing records;
they are **not** crossing-level recall or precision measurements. In
`top_earlgrey`, 263 of 327 crossing records involve an `auto_port_*` domain;
many others use clock-manager output or local `clk_i` names rather than the
golden top-level labels. This is evidence of unresolved clock lineage, not
evidence that the six reference pairs have no RTL crossings. A scope-aware
`Ac_cdc09` check suppressed 1,942 cautions previously produced by global
leaf-name matching: confirmed examples mistook ordinary `d_i` data ports for
clocks declared in unrelated modules. This reduction is not a manual
adjudication of every removed record. One `Ac_cdc09` caution remains for review.
The 12 violations and 246 cautions are analyzer classifications, not manually
adjudicated defects.

The raw reports and logs are generated under `bench/opentitan/results/` and
are intentionally ignored by Git. The scheduled/on-demand benchmark workflow
uploads them as a CI artifact. This snapshot makes the current SoC behavior
visible; labeled signal-level ground truth, clock-lineage validation, and
review of generic-primitive substitution are still required before claiming
accuracy against a production OpenTitan build.
