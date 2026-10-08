# OpenTitan benchmark snapshot — 2026-10-08

The source-only run used pinned OpenTitan `earlgrey_silver_release_v5`
(`ed044fc9760bdf9fc075d0015ba1db07fa075355`) and a dirty svlens worktree
based on `dc81854adcc4b6148ee7abc3b06d7b946c5ec474`. The analyzed binary
SHA-256 was `b3526211a562c95458505876a33a88e0494827e29291f57bcc9d571b5cc02c38`.
The audited report was generated on 2026-10-07 19:07 UTC (2026-10-08 in Korea).
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

### Follow-up: one-stage continuous aliases

A clean-tree rerun at 2026-10-07 19:58 UTC used svlens commit
`68b76c6c15ecf417428af6e0fd3cb1872b376766` and the same analyzer binary
SHA-256 shown above. The source oracle now includes one direct continuous
alias only when its simple-logic width matches both sibling ports, the source
port is its sole output driver, and no other assignment writes the alias.
Three additional RTL-backed paths appear as `direct`:

| Scope | Source → destination | RTL evidence |
|---|---|---|
| AES shadow register | `committed_reg.q[11:0]` → `wr_en_data_arb.q[11:0]` | `prim_subreg_shadow.sv:55–61,136–154` |
| USB device | `usbdev_rxfifo.rvalid_o` → `intr_hw_pkt_received.event_intr_i` | `usbdev.sv:225–244,744–747` |
| Ibex core | `id_stage_i.alu_operand_a_ex_o[31:0]` → `cs_registers_i.csr_wdata_i[31:0]` | `ibex_core.sv:534,911,947` |

The extended frame passes 358/358 direct paths, zero approximate-only paths,
and zero missing paths. Its pinned expected-pair SHA-256 is
`489f2ee89a2927b87654a85211290e57718a643de2ebcb77eba12248341026f3`.
The previous 355/355 table describes the earlier run; analyzer findings are
unchanged. The new probe still excludes multi-stage, computed, conditional,
and procedural glue as well as interface/modport ports; it is not a whole-SoC
recall estimate.

### Follow-up: named constants are not signal aliases

A second clean-tree run at 2026-10-07 20:12 UTC used svlens commit
`8ed152afecabdd50d6e47ad3c8b3ab953e0180ff` and binary SHA-256
`1fb743d04de0b60308cea53bf1435c27a3c44d627b87d011cd938834fddcbb7c`.
Treating enum values and parameters as tie-offs removed 53 approximate
connection rows and added none compared with the prior report: the SoC now
has 19,349 connections (8,692 direct, 10,657 approximate) and 27,175
bit-flow gaps. The removed rows had been joined through constant symbols;
they were not individually adjudicated as a whole-design precision sample.
Two source-checked nonpaths between the separate KMAC and entropy-source SHA3
instances now raise the SoC absent-path gate to 7/7. The fixed direct-path
frame remains 358/358.

All 20 selected sample IDs and their source-backed verdicts remained the same.
Only the population changed: the four stratum counts are 2,538 direct/unranged,
6,154 direct/ranged, 6,202 approximate/unranged, and 4,455
approximate/ranged, with SHA-256
`32625e259f9208b86d50972cd6a191a598e3d723d9487b9bee745401e4ba7445`.
The 19-channel AXI-lite modport regression checks direct paths and reverse- or
cross-bus nonpaths, including enum-valued response members. It is still a
fixture, not an AXI SoC accuracy estimate.

The five CDC signal references and 1/1 category probe are unchanged. The
supplemental SoC period projection still populates 157/243 timing bases and
matches 4/4 root periods; it does not prove capture timing. Separate SVA runs
retain the primary crossing classifications: AES's data-hold reference passes
1/1 with 2/2 JSON/assertion labels, and the SoC's 2FF/FIFO Gray, write/read
no-step, req-ack, and data-hold references pass 6/6 with 399/399 labels.
Generated-array FIFO properties use the pointer FF's declaration scope;
the full SoC SVA elaborates with zero errors and the same 12 upstream RTL
warnings as the no-SVA baseline. An enabled receiving-FF fixture remains
structurally `two_ff/INFO` but emits no unconditional stage-transfer
assertion or JSON assertion ID; the pinned SoC label counts remain unchanged.
SVA elaboration is not simulation or
a protocol-safety proof. Raw benchmark artifacts remain under ignored
`bench/opentitan/results/` and are uploaded by the scheduled/on-demand CI job.

### Follow-up: CDC source resolution keeps the ancestor port chain

The first GitHub Actions run of the benchmark workflow (2026-10-08 10:10 UTC)
aborted every CDC target (aes, hmac, uart, top_earlgrey) with SIGABRT. `make
build` configures without `CMAKE_BUILD_TYPE`, so asserts are enabled, and
`resolveToFFs` recursed through a continuous assign without forwarding the
ancestor port chain, tripping the chain-invariant assert in `findFFByName`.
NDEBUG builds instead skipped the ancestor walk, so earlier local snapshots
were produced with that walk silently disabled for sources reached through a
local assign.

A Release rerun at svlens commit `a9430e1c24c2a89b991c0ef1d188e5548953e89d`
(binary SHA-256
`aad249ecb8cc42a6a24cc6c5596dc1a11feb0d93470942f059d7dd9e0ec05a37`; the only
untracked worktree entry was a symlink to the shared `.ot-src` checkout)
forwards the chain. Against a Release build of the parent commit, which
reproduces this snapshot's 3 / 219 / 21 CDC counts and 157/243 timing bases,
the only change is one added SoC crossing, now 3 / 220 / 21 (244 records):

- `top_earlgrey.u_lc_ctrl.transition_token_q` (`clk_io_div4_timers`, root
  `clk_io_i`) -> `top_earlgrey.u_flash_ctrl.u_lfsr.lfsr_q` (`clk_main_infra`,
  root `clk_main_i`), `CAUTION` / `Ac_cdc02`. Source path: `lc_ctrl.sv:397`
  assigns `lc_flash_rma_seed_o` from `transition_token_q`, `top_earlgrey.sv`
  wires it to `flash_ctrl.rma_seed_i` (lines 1666, 2084), and
  `flash_ctrl.sv:276` feeds it to `prim_lfsr.seed_i`, loaded by
  `prim_lfsr.sv:368` when `seed_en_i` is set. That enable is qualified through
  `prim_lc_sync`, so this is a structural review item, not a demonstrated
  synchronization failure.

aes, hmac, and uart CDC reports are unchanged. The five signal references
(5/5 roots, 1/1 category), 3/3 guidance and 1/1 reset-unresolved probes,
4/4 root periods, AES 1/1 and SoC 6/6 SVA references, and 399/399 SoC
assertion labels are unchanged; the period projection now reports 157/244
timing bases. Connectivity results, the 20-row sample, and the 358/358
direct-wiring frame do not depend on this CDC path and are unchanged.
Following single-signal parent-scope aliases between sibling instances
(the companion fix in the same change) adds no further OpenTitan
difference: a Release build with both fixes produces identical reports.
