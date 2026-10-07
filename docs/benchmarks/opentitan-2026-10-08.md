# OpenTitan benchmark snapshot — 2026-10-08

The source-only run used pinned OpenTitan `earlgrey_silver_release_v5`
(`ed044fc9760bdf9fc075d0015ba1db07fa075355`) and a dirty svlens worktree
based on `5e30010958fab89b9bfdd648b57504ef361064c1`. The analyzed binary
SHA-256 was `15ad65a4570c2d7a79e3d5813c5ae3d6e05903841f27d12ccee10160f5d2ccce`.
The report was generated on 2026-10-07 18:17 UTC (2026-10-08 in Korea).
Counts are analyzer output, not a whole-design precision or recall estimate.

| Target | Connections (direct / approximate) | Bit-flow gaps | Required paths | Absent paths | CDC (violation / caution / info) |
|---|---:|---:|---:|---:|---:|
| aes | 642 (217 / 425) | 730 | 7/7 | 2/2 | 0 / 2 / 2 |
| hmac | 95 (46 / 49) | 202 | 3/3 | 2/2 | 0 / 0 / 0 |
| uart | 139 (56 / 83) | 170 | — | — | 0 / 0 / 0 |
| top_earlgrey | 19,402 (8,692 / 10,710) | 27,183 | 16/16 | 5/5 | 3 / 219 / 21 |

Compared with the [2026-10-03 snapshot](opentitan-2026-10-03.md), indexed
`+:` / `-:` part-selects now interpret Slang's operands as base and width,
including genvar-resolved arithmetic. SoC direct rows increased by 45 and
approximate rows did not change. Recorded SoC bit-flow gaps fell by 802:
`unresolved_destination_range` fell by 146 and
`unresolved_source_range` by 720, while `nonstructural_source` rose by 64 as
newly resolved destinations exposed computed sources. These are assignment
instance counts, not recovered-path counts.

The pinned `aes_prng_masking.sv:76–83,117–128` path supplies one 160-bit FIFO
seed to five generated 32-bit LFSR inputs. Five exact and exclusive source
lanes (`0–31`, `32–63`, `64–95`, `96–127`, `128–159`) pass in standalone AES
and again inside `top_earlgrey` (5/5 in each hierarchy). Paired fixtures also
check indexed `+:` / `-:` selection, ascending declarations, modport member
overlap, and that a runtime base remains range-free approximate may-flow.
These selected paths do not validate all new SoC rows or establish SoC-wide
accuracy.

The five CDC signal references and 1/1 category probe are unchanged. The
supplemental SoC period projection still populates 157/243 timing bases and
matches 4/4 root periods; it does not prove capture timing. Separate SVA runs
retain the primary crossing classifications: AES's data-hold reference passes
1/1 with 2/2 JSON/assertion labels, and the SoC's 2FF/FIFO/req-ack/data-hold
references pass 4/4 with 320/320 labels. SVA elaboration is not simulation or
a protocol-safety proof. Raw benchmark artifacts remain under ignored
`bench/opentitan/results/` and are uploaded by the scheduled/on-demand CI job.
