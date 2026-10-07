# OpenTitan benchmark snapshot — 2026-10-08

The source-only run used pinned OpenTitan `earlgrey_silver_release_v5`
(`ed044fc9760bdf9fc075d0015ba1db07fa075355`) and a dirty svlens worktree
based on `78153372b76233d59e987e05d5e5ec703c26bae5`. The analyzed binary
SHA-256 was `15ad65a4570c2d7a79e3d5813c5ae3d6e05903841f27d12ccee10160f5d2ccce`.
The audited report was generated on 2026-10-07 18:48 UTC (2026-10-08 in Korea).
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

## Connection sample audit

[`sample_connections.py`](../../bench/opentitan/sample_connections.py) selects
the five lowest SHA-256-ranked connection rows in each direct/approximate ×
ranged/unranged stratum using seed `svlens-accuracy-v1`. Sorting canonical
rows makes the selection independent of report order; duplicate rows retain
their multiplicity. The pinned population hash is
`baedbadcc682a5bc4c116c24a9dcc3ad7d30d63147dcfcc27bfea91af5c16b8d`.
RTL-backed verdicts are in
[`top_earlgrey_connection_sample.yaml`](../../bench/opentitan/golden/top_earlgrey_connection_sample.yaml).

| Stratum | Population | Sampled | Confirmed | Contradicted | Unresolved |
|---|---:|---:|---:|---:|---:|
| Direct, no bit range | 2,538 | 5 | 5 | 0 | 0 |
| Direct, ranged | 6,154 | 5 | 5 | 0 | 0 |
| Approximate, no bit range | 6,255 | 5 | 5 | 0 | 0 |
| Approximate, ranged | 4,455 | 5 | 5 | 0 | 0 |
| Total | 19,402 | 20 | 20 | 0 | 0 |

The Ibex `data_we_o` row reaches generated lockstep buffer bit 513.
`ibex_top.sv:525–617` places it immediately above 30 lower concatenation
operands; their widths sum to 513 bits, including the 128-bit `crash_dump_t`
defined in `ibex_pkg.sv:15–20`. The apparent
counter-to-mcycle approximate path was confirmed as a possible combinational
port influence via CSR readback and SET/CLEAR (`ibex_cs_registers.sv:435–449,
729–745,1222–1262`), not as a claim that the mcycle FF necessarily captures
it. This audit measures 20 selected report rows only; generated structures
are correlated and no non-reported paths were
sampled. It must not be extrapolated to whole-design precision or recall.

## Source-derived direct-wiring recall probe

A second, independent frame walks the pinned elaborated Slang AST rather than
sampling report rows. It includes single-bit or whole-width one-dimensional
`logic` ports (up to 4,096 bits) on sibling instances connected to the same named signal with
exactly one output driver and matching widths. The seven configured SoC IP
scopes produce 355 distinct expected paths:

| Scope | Expected | Reported direct |
|---|---:|---:|
| AES | 21 | 21 |
| SPI device | 47 | 47 |
| HMAC | 10 | 10 |
| SPI host 0 | 33 | 33 |
| USB device | 88 | 88 |
| Ibex core wrapper | 135 | 135 |
| Power manager | 21 | 21 |
| Total | 355 | 355 |

No expected pair was missing or only approximate. The expected-pair SHA-256 is
`f4d18eb380230608f7fe3776f4dd68e5222e97d1f9fd19cc9d324300599579b9`;
the complete regenerated pair manifest is saved in the benchmark artifact at
`results/top_earlgrey/source_recall.json`. The benchmark gates both that frame
hash and all 355 direct paths, so a shrinking oracle requires re-adjudication.
This checks bounded named-net path presence, not vector bit correspondence or
whole-SoC recall. It does not adjudicate sliced/converted buses,
conditional/procedural paths, modports, or IPs outside the chosen scopes.

The five CDC signal references and 1/1 category probe are unchanged. The
supplemental SoC period projection still populates 157/243 timing bases and
matches 4/4 root periods; it does not prove capture timing. Separate SVA runs
retain the primary crossing classifications: AES's data-hold reference passes
1/1 with 2/2 JSON/assertion labels, and the SoC's 2FF/FIFO/req-ack/data-hold
references pass 4/4 with 320/320 labels. SVA elaboration is not simulation or
a protocol-safety proof. Raw benchmark artifacts remain under ignored
`bench/opentitan/results/` and are uploaded by the scheduled/on-demand CI job.
