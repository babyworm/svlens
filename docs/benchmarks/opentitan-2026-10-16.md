# OpenTitan benchmark snapshot — 2026-10-16

A clean-tree run at 2026-10-15 15:02 UTC (2026-10-16 KST) used pinned
OpenTitan `earlgrey_silver_release_v5` at
`ed044fc9760bdf9fc075d0015ba1db07fa075355`, svlens commit
`575c1256a8bd4de1acbc811b12ab2e1750b789d7`, and svlens binary SHA-256
`fe7cce85ac8e4e6dda1885c9455b9a2291485d4c0290590fc2d12552c0e7ce54`.
The source-only benchmark uses generated generic primitive aliases; it is not
a production technology-library build.
The [on-demand branch run](https://github.com/babyworm/svlens/actions/runs/38756662114)
also passed benchmark evaluation, AES/SoC SVA elaboration, and artifact upload.

| Scope | Connectivity | CDC (violation / caution / info) | Bounded gates |
|---|---:|---:|---|
| AES | 642 (217 direct / 425 approximate) | 0 / 2 / 2 | 7/7 required, 2/2 absent, SVA 1/1 |
| HMAC | 95 (46 / 49) | 0 / 0 / 0 | 3/3 required, 2/2 absent |
| UART | 139 (56 / 83) | 0 / 0 / 0 | — |
| top_earlgrey | 19,349 (8,692 / 10,657) | 3 / 220 / 21 | 16/16 required, 7/7 absent, CDC 6/6, SVA 6/6 |

The SoC's source-derived simple direct-wiring frame remains 358/358. Its
20-row stratified connection sample still has 20 confirmed and no contradicted
or unresolved rows; the population SHA-256 remains
`32625e259f9208b86d50972cd6a191a598e3d723d9487b9bee745401e4ba7445`.
The SoC SVA run links all 399 emitted assertion labels to JSON IDs and keeps
the primary crossing classifications. The projected period context populates
four base clocks and 157/244 crossing timing bases; it does not measure path
delay or prove data stability.

## Newly recovered CDC path

Preserving the ancestor port map through recursive continuous-assignment
resolution adds one previously missed `CAUTION` record:

`top_earlgrey.u_lc_ctrl.transition_token_q` →
`top_earlgrey.u_flash_ctrl.u_lfsr.lfsr_q`, with top-clock roots `clk_io_i` →
`clk_main_i`.

The pinned RTL assigns `transition_token_q` into `lc_flash_rma_seed_o`
(`lc_ctrl.sv:397`), connects that signal to `flash_ctrl.rma_seed_i`
(`top_earlgrey.sv:1666,2084`), and feeds the flash LFSR's `seed_i`
(`flash_ctrl.sv:272–276`). The benchmark now gates this exact FF path and both
roots as its sixth SoC CDC reference. The observed `CAUTION` classification
is not a verdict on the RMA seed transfer protocol or capture timing.

Compared with the [2026-10-08 snapshot](opentitan-2026-10-08.md), SoC CDC
records rise from 243 to 244 (cautions 219 to 220); the connection population
is unchanged. An ASan/Debug run of AES and top_earlgrey CDC completes without
the ancestor-chain assertion that had stopped the first remote benchmark run.
This selected path and the other benchmark gates do not estimate whole-SoC
CDC precision or recall.

## Follow-up: directed flow through ordinary module ports

A clean-tree rerun at 2026-10-15 15:39 UTC used svlens commit
`2566a5fef9f708be8fefa40492afb7114ecde67b` and binary SHA-256
`91ec66583bbbf72e18c865c9f751adc34a4523f2e9fe4c630dd8f2dae58b00b8`.
The [on-demand rerun](https://github.com/babyworm/svlens/actions/runs/38762218763)
also passed benchmark evaluation, AES/SoC SVA elaboration, and artifact upload.
The analyzer now follows simple same-width combinational copies across module
input/output boundaries while leaving clocked-register boundaries intact.

| Scope | Previous connections | New connections (direct / approximate) |
|---|---:|---:|
| AES | 642 | 983 (392 / 591) |
| HMAC | 95 | 107 (51 / 56) |
| UART | 139 | 165 (82 / 83) |
| top_earlgrey | 19,349 | 28,010 (13,406 / 14,604) |

The SoC gained 8,661 bit-ranged rows (4,714 direct and 3,947 approximate)
with no row removed. The direct named-net/source-alias frame remains 358/358;
it is not an oracle for every new through-module path. Four new rows entered
the deterministic 20-row connection sample and were checked against pinned
RTL:

| Newly sampled relation | Source evidence |
|---|---|
| Pinmux register-interface write-data bit 0 → `u_dio_pad_sleep_en_20.wd` | `top_earlgrey/ip/pinmux/rtl/autogen/pinmux_reg_top.sv:95,17735–17741,23237` |
| Lockstep buffer bit 605 → shadow-core boot-address bit 20 | `ibex_top.sv:44,525–617,666–670`; lower 513 bits precede `data_we_o`, then one `data_we_o` bit and 71 control/data bits place boot-address bit 0 at 585 |
| Ibex cache data-write output → generated RAM-bank `write_i` | `ibex_top.sv:255,394–398`, `ibex_core.sv:84` |
| PLIC register-interface write-data bit 26 → `u_ie0_4_e_154.wd` | `top_earlgrey/ip/rv_plic/rtl/autogen/rv_plic_reg_top.sv:95,20204–20210,22908` |

The other 16 sample IDs retain their earlier verdicts, leaving 20 confirmed
and zero contradicted or unresolved. The new population SHA-256 is
`09758d7ecfa2673c2854697d592d9477571fda60216b6c95c753fe23d8d0cb74`.
These four checked examples do not establish precision for the other 8,657
new rows or whole-design recall. CDC counts and pinned CDC/SVA gates are
unchanged from the first run above.

## Follow-up: source-derived pass-through recall

The local 2026-10-16 run adds a second Slang-AST recall frame without
changing analyzer output or the existing 358/358 direct-wiring frame. It
enumerates sole, same-width continuous module input-to-output copies through
at most four modules. The expected-pair SHA-256 is
`63a25fad14dc617c8e876b5f516bb6f9a30f223f461b7c2709c9262ce3110977`;
all 16 expected paths appear as `direct` rows: 11 in SPI device, three in SPI
host, and two in Ibex. None overlap the older 358-pair frame. The benchmark
now fails if either frame changes, or if one of its paths is missing or only
approximate.

This is a narrow check of pass-through path presence, not bit-lane correctness
or whole-SoC recall. Eleven expected paths fan out from the SPI device clock
mux output, three are SPI host handshake relations, and two are Ibex ready
relations. It does not cover AXI/modport signal lanes, computed or procedural
glue, multiply driven paths, or modules outside the selected seven scopes.

## Follow-up: mux controls and clocked-state boundary

An RTL-backed negative test exposed a false combinational path through an
`always_ff` D/Q alias in the same scope. Removing that alias and adding
range-free `approximate` dependencies from runtime `?:` selectors changed the
SoC report from 28,010 to 24,095 connections (13,407 direct, 10,688
approximate). Relative to the preceding committed artifact, 3,989 coarse
approximate rows disappeared and 74 rows appeared, including one direct row
previously tainted by an approximate alias. The initial new sample had a
flash-read-ready → request-type path through an FSM register; the source RTL
contradicted that combinational interpretation, and the FF-boundary fix
removed it. This is a correctness repair, not a measured whole-SoC precision
improvement.

Three old sampled D→Q rows were previously labeled confirmed. Their values
may change across clock edges, but they are not combinational connections
and have been retired from the refreshed 20-row sample. The three replacement
rows are source-backed: two Ibex counter outputs can reach other counters'
`counter_val_i` ports through the indexed CSR read and shared SET/CLEAR
write-data mux (`ibex_cs_registers.sv:435–449,729–745,1251–1262`), without
implying their write-enable is asserted; the alert receiver ACK buffer's
aggregate output reaches the decoder's `diff_ni` input through `ack_n`
(`prim_alert_sender.sv:100–116`). The new sample population SHA-256 is
`ee49c6049046584911430013ebe88962f66a1e65b50f42d0cc0e02c9b4ce9f14`;
20/20 selected rows are confirmed. Two negative SHA3 cross-instance probes
now use `keccak_complete_i`, an observed endpoint with a direct local source,
instead of `process_i`, which ceased appearing in any connection row after
the FF fix. An I2C FIFO-valid → format-byte mux control path is a new positive
SoC reference (`i2c_core.sv:285,311,420`), bringing exact-path gates to 17/17.
All seven negative probes, 358/358 direct-wiring paths, and 16/16 pass-through
paths remain gated. The bit-flow gap count remains 27,175.
