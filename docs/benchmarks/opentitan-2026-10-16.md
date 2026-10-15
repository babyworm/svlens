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
