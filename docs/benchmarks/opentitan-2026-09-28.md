# OpenTitan benchmark snapshot — 2026-09-28

OpenTitan `earlgrey_silver_release_v5` at
`ed044fc9760bdf9fc075d0015ba1db07fa075355`; svlens `0.3.6` from a
dirty worktree based on `5e30010958fab89b9bfdd648b57504ef361064c1`.
Analyzed binary SHA-256:
`0a1e3ce616a09832cc8f70a83bdc9dedf5968db4231bfd9040d294855edd3f8c`.
The local run used the source-only FuseSoC filelists, `SYNTHESIS`,
`--single-unit`, and benchmark-local aliases of OpenTitan generic primitives.
All four targets produced fresh conn and CDC JSON reports. Times are
single-run macOS wall times, not cross-machine performance comparisons.

| Target | Conn edges | Direct | Approximate | Bit-flow gaps | Conn reference | Conn active issues | CDC violations | CDC cautions | CDC info | CDC references / roots | Local pair probes | Root pair probes | Conn / CDC time |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| aes | 622 | 210 | 412 | 771 | 2 / 2 | 100 | 0 | 2 | 2 | — | 2 / 2 | 2 / 2 | 89 / 78 ms |
| hmac | 93 | 46 | 47 | 213 | 1 / 1 | 117 | 0 | 0 | 0 | — | no probes | no probes | 44 / 24 ms |
| uart | 139 | 56 | 83 | 175 | — | 78 | 0 | 0 | 0 | — | no probes | no probes | 48 / 42 ms |
| top_earlgrey | 18,501 | 8,647 | 9,854 | 29,133 | 8 / 8 | 8,464 | 3 | 219 | 21 | 4 / 4, 4 / 4 | 0 / 6 | 2 / 6 | 1,352 / 872 ms |

This run uses declaring-scope connectivity keys and skips uninstantiated
generate branches. Relative to the 2026-09-26 snapshot, conn edge counts rose
on all four targets; a small set of labeled paths does not turn the added
edges into a measured recall gain. Constant unpacked-array element mapping initially
removed one HMAC approximate edge. RTL inspection showed that it represented a
real path from `u_socket.tl_d_o` through `tl_socket_h2d[1]` to `u_reg_if.tl_i`.
The coarse whole-array dependency was restored alongside the exact element
mapping, and the benchmark now gates this manually labeled path (1 / 1).
The clock-lineage walk now also traverses instantiated generate scopes and
resolves a signal by its declaring scope before an `auto_port_*` fallback.
This change is covered by distinct-lane and inactive-branch fixtures, but it
did not change this OpenTitan snapshot's crossing or root-label counts.
The current generated-clock period propagation and divider self-feedback fix
also leave these benchmark counts unchanged; `make bench` supplies no SDC, so
this run does not validate period-derived `Ac_cdc08` hints on OpenTitan.
Four SoC FIFO pointer crossings are also signal-level references: the
crossbar's main/IO async FIFO in both directions and USBDEV's IO/USB async
FIFO in both directions. Their source FF, first synchronizer FF, and clock
port wiring were checked against the pinned OpenTitan RTL; the benchmark
finds all four paths and all four expected root pairs. This is a small
presence/root-label sample, not whole-SoC recall or precision.
10,288 SoC report rows expose ordinal bit ranges, including generated per-bit
buffers, nested constant packed/unpacked-array elements, packed-struct fields,
and positional concatenation chains. Relative to the preceding local binary,
mapping packed selections within constant unpacked elements adds 215 direct
range rows before 53 redundant full-width coarse direct rows are removed;
reported bit-flow gaps fall by 1,492. Some range rows refine an existing
connection between the same ports; neither the net row delta nor the gap delta
is a measured recall gain. The analyzer reports 8,647 direct
and 9,854 approximate SoC rows; these are evidence classes, not measured
precision or recall. The bit-flow graph is capped at 4,096 bits per assignment
and does not prove arbitrary casts or conditional timing behavior.
Two AES paths are checked against `aes_cipher_core.sv`: SubBytes data/mask
outputs feed separate unpacked share elements and the share-0/share-1
ShiftRows inputs, respectively, across all 128 ordinal bits. Both exact
range rows appear in the AES IP report (2/2) and SoC report (2/2); other new
range rows have not been individually adjudicated against pinned RTL.
Six EDN response lanes are hand-checked against the pinned top-level RTL and
`edn_rsp_t` width: each `u_edn0.edn_o` slice reaches its intended child port
with the expected 34-bit source offset, destination bits [33:0], and `direct`
kind, with no conflicting row for the same port pair (6 / 6). The untested
lane 2 feeds a top-level AST output rather than a
child input. These references are a bounded mapping check, not full accuracy.
The new gap count is elaborated assignment instances with incomplete positional
mapping, not missing edges. It excludes signal-free tie-offs and retains up to
four source examples per reason. In the SoC, the leading reasons are
`nonstructural_source` (26,382), `unresolved_destination_range` (1,263), and
`unresolved_source_range` (1,079); 394 are width-changing conversions and two
exceed the 4,096-bit cap. These counts expose where the current bit-flow model
needs work, without implying that every such assignment affects a port-to-port
connection.
It also includes fixed-point propagation through direct assignments and directed
module ports, including clock-named packed-struct output fields. It does not
merge conditional muxes or clock gates with their inputs. Separate root
provenance follows recognized one-input gates and dividers without changing
crossing classification. A generated-block FF now resolves its nearest scoped
clock net before considering a global same-named clock. CDC connectivity no
longer guesses an FF source from any sibling descendant with a matching leaf
name, and a mapped input port takes precedence over an inherited same-named
wire. In the SoC report, 220 of 243 crossing records have both root labels;
two of the six topology probes occur in that
provenance; none occur as pairs of local top-level domain labels. These are
presence probes, not crossing-level recall or precision. The third
root-provenance pair in the prior local run disappeared with a USB-to-RAM
false edge, illustrating why pair presence is not accuracy. Seventy-three
records involve an `auto_port_*` domain (the
[previous snapshot](opentitan-2026-09-26.md) had 263 of 327). Relative to the
earlier 2026-09-28 run, the scoped-clock fix changed SoC counts from
12/277/24 to 5/219/23 (VIOLATION/CAUTION/INFO), then removing the
name-only connectivity fallback yielded 3/219/21. These changes are
fixture-backed, but not a manual adjudication of every SoC path.

Four of six topology-only domain-pair probes remain absent even in root
provenance; they have not been verified as actual signal crossings and should
not be counted as false negatives. Twenty-three crossing records still lack
at least one root label. Several use JTAG or SPI clocks that pass through
functional/scan muxes (`tck_muxed`, `clk_spi_in_buf`, `clk_spi_out_buf`, or
`sram_clk`); the pinned RTL does not establish one mode-independent root for
those outputs. OpenTitan's clock manager has
conditional and generated-scope paths beyond this model. Generic-primitive
substitution is not equivalent to a production technology-library build.
The direct-alias reset-driver check adds no `Ac_cdc06` record or resolved FF
driver among 7,473 reset-usage paths in this run; fixture-backed end-to-end
tests exercise the new optional driver fields. This is not evidence that the
SoC has no reset-domain hazard, because conditional and external reset trees
remain outside the model.
In a separate `--emit-sva` run of this binary, 174 stage-transfer, 130
`prim_fifo_async` Gray-pointer, and ten `prim_sync_reqack` ACK-requires-REQ
assertions were generated. The 126 overlapping FIFO crossings link both
stage and Gray IDs. Generated SVA and the pinned OpenTitan RTL elaborated
together with zero errors and 12 pre-existing upstream implicit-conversion
warnings. These properties check pointer encoding, sampled FF transfer, or an
ACK integration contract; they do not prove metastability, eventual response,
or whole-protocol safety. The timed `make bench` run above did not enable SVA
output.
The remaining three VIOLATION records cover two SPI in/out paths and one
AON-to-IO power-manager path; they have not been manually adjudicated. The
power-manager record now exposes its conditional capture control
`pwrup_cause_chg` with an `Ac_cdc08` stability-review hint, without changing
its VIOLATION category. More signal-level references and clock-manager
lineage validation remain necessary.

Raw reports and logs remain under ignored `bench/opentitan/results/`; the
scheduled/on-demand CI workflow uploads them as an artifact.
