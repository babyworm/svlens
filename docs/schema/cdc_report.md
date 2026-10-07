# cdc_report.json schema contract (stable-now)

This document records the **stable-now** Phase 1B contract.
Additional confidence / rationale fields remain provisional until CDC analytical-trust
milestones freeze them.

## Top-level keys
- `summary`
- `domains`
- `reset_usage`
- `crossings`

## `summary`
- `violations`
- `cautions`
- `conventions`
- `info`
- `waived`

## `domains[*]`
- `name`
- `source` -- clock source name for this domain
- `root_source` -- top-level clock provenance when structurally established;
  otherwise empty. This is not a CDC safety classification.
- `period_ns` -- finite positive period attached to this domain's clock source,
  or `null` when unknown. A declared/derived period is timing context, not a
  measured delay or a CDC safety classification.

## `reset_usage[*]`

- `signal` -- reset path observed at FF sinks
- `asynchronous` -- whether any sink uses it asynchronously
- `polarity` -- `active_low`, `active_high`, or `mixed`
- `ff_count` -- number of FF nodes using this reset
- `dest_domains` -- sorted destination-domain names
- optional `driver_ff`, `source_domain` (provisional) -- emitted together only
  when every sink resolves to the same unique FF with a known source domain.
  Supported routes include direct assignments/ports, simple one-bit inversions
  (including one unconditional `always_comb` assignment), and one fixed-selected
  vector bit feeding a scalar reset port. A distinct FF driving that indexed
  output bit can also be identified without choosing a sibling bit's driver.
  Omission means the driver is
  unresolved or conflicting, not that the reset is safe.
- optional `driver_inverted` -- emitted with unique driver provenance when
  every sink agrees on inversion parity from the FF output to the reset pin:
  `false` for direct/even inversions, `true` for odd one-bit inversions.
  Conflicting parity omits the driver and parity fields. It does not prove
  reset assertion/deassertion timing or a safe RDC protocol.
- optional `conditional_muxes` -- sorted structural candidates reachable
  backwards from the reset sink through direct, selected-bit, and inversion
  aliases. Each object has exact hierarchical `output`, `input0`, `input1`,
  and `select` port paths of a signature-checked `prim_clock_mux2` or
  `prim_generic_clock_mux2`. The mux itself is nontransparent: both inputs
  are candidates, not a chosen runtime branch or a unique reset driver. A
  reachable mux also blocks `Ac_cdc06` from claiming a unique FF through a
  separate direct alias path.
  Optional `input0_source`, `input1_source`, and `select_source` give the
  immediately connected hierarchical net or constant-selected integral
  element when that expression is structurally direct. Optional sorted
  `input0_dependencies`, `input1_dependencies`, and `select_dependencies`
  list runtime signals read by a computed expression instead; they are
  may-influences, not direct bit correspondence. Width- or state-changing
  casts are not transparent aliases. An absent source/dependency field is
  unresolved, not a tie-off or proof of independence.
  Optional `selected_input: 0|1` means the mux selector evaluates to a known
  elaboration-time value; reset-driver tracing follows only that input. A
  selector that is runtime-dependent or X/Z leaves this field absent and
  blocks a unique FF claim. A `driver_ff` can coexist with a known-selected
  mux only when its selected input independently resolves to a unique FF;
  an unrelated direct FF alias cannot stand in for an external selected
  branch. This still does not establish reset deassertion safety. Competing
  immediate drivers and cycles remain unresolved.
  Optional `input0_candidate_ffs` and `input1_candidate_ffs` list sorted
  `{path, domain?, inverted?}` FF ancestors of each structurally traceable
  input. `inverted` is parity from that FF output to the mux input, not to
  the downstream reset sink. If both direct and inverted paths reach the same
  FF, `inversion_ambiguous: true` replaces `inverted` rather than choosing one.
  Runtime selectors may list candidates on both branches; a fixed selector
  lists candidates only on its selected branch. These are candidates, not
  `driver_ff` for the mux output. A scalar `~` or `!` at an input is traced
  as a reset-only inversion, while wider inversions and other computed
  expressions can have no listed FF despite reading one. The trace stops at
  FF boundaries and does not merge clock domains.
- optional `conditional_mux_trace_truncated: true` -- traversal reached the
  4,096-node bound; candidate muxes or FF ancestors may be missing from this
  record.

This is a sink-side reset inventory with limited registered-driver and mux
topology evidence. It does not trace the full reset tree or classify all
reset-domain crossings.
Separately, `Ac_cdc06` reports a
unique FF-generated asynchronous reset reaching a different clock domain
through direct assignments, directed module ports (including scalar reset
fields of packed structs), exact hierarchical
references, or simple one-bit inversions.
Inversions are reset-only provenance; conditional/multiply assigned procedural
resets and conflicting polarity paths remain unresolved. The rule does not
prove a reset synchronizer is absent, nor establish external reset lineage or
safe deassertion timing.
The fixed-selected vector-to-scalar route is not a complete reset-array tree.
Dynamic selectors, duplicate FF paths, and nontransparent reset logic stay
unresolved; a selected bit does not establish timing or mode safety.

## `crossings[*]`
- `id`
- `source` -- for `Ac_cdc09` clock-as-data cautions, the resolved hierarchical
  clock-net path when available, rather than an unscoped leaf name
- `dest`
- `source_domain`
- `dest_domain`
- `source_root_domain`, `dest_root_domain` -- top-level clock provenance when
  established through direct aliases or recognized single-input clock
  gates/dividers; empty when unresolved or ambiguous. Equal root labels do
  not imply the local domains are synchronous or safe to cross.
- `capture_conditions` -- signals in conditions guarding assignments to the
  destination FF. This is a review inventory, not proof that the enable is
  synchronized or that source data is stable when captured. A conditional
  wide-bus async crossing can carry an `Ac_cdc08` recommendation while
  remaining a `VIOLATION`.
- `path`
- `category`
- `severity`
- `sync_type`
- `rule`
- `recommendation` -- for an unsynchronized multi-bit async crossing, this points to a
  coherent bus-transfer scheme rather than independent per-bit 2FFs; it does
  not verify a bundled-data hold interval, Gray constraints, or FIFO protocol.
  For a single-bit unsynchronized crossing, it asks for clock-relationship and
  signal-semantics review before suggesting a level synchronizer or event
  transfer; neither capture assumptions nor mode-dependent timing are proven.
- `relationship` -- one of `asynchronous`, `inferred_asynchronous`,
  `synchronous_same`, `divided`, `phase_offset`,
  `physically_exclusive`, `logically_exclusive`, `gated`. An exclusive label
  by itself leaves an unsynchronized crossing at `CAUTION`; it is not a CDC
  waiver or proof that data cannot be captured across a mode change.
- `rationale` -- human-readable string explaining why the relationship
  was inferred or asserted
- `timing_basis_ns` -- nullable number; the destination clock period when
  known, otherwise the source period, in nanoseconds. A period may be derived
  from a uniquely resolved SDC generated-clock divide/multiply chain or a
  recognized unconditional or reset-only RTL toggle divider. Periods can add an `Ac_cdc08` fast-to-slow
  review hint, but do not include phase or path delay and cannot prove safe
  capture timing.
- `sdc_false_path` -- boolean; an exact directional clock-name match for an
  SDC `set_false_path` whose `-from` and `-to` both use `get_clocks`. Pin- and
  port-targeted exceptions are not projected onto an entire domain pair. The
  flag records a declared timing exception, not measured timing or proof that
  the crossing is synchronized or safe. It does not alter category or severity.
- `sdc_max_delay_constraint_ns` -- nullable number; one finite positive,
  directional clock-to-clock `set_max_delay` declaration when uniquely
  matched. This is a requested upper bound, not measured path delay or proof
  of data stability. It does not alter CDC category or severity.
- `sdc_max_delay_datapath_only` -- boolean; whether the uniquely matched
  declaration specified `-datapath_only` (false when no unique match exists).
- `sdc_max_delay_ambiguous` -- boolean; more than one declaration matches or
  either matched clock name resolves to multiple sources. No numeric bound
  is selected in that case. STA must resolve exception applicability and
  precedence, especially alongside false paths or clock groups.
- optional `source_file`, `source_line`, `source_column`, `dest_file`,
  `dest_line`, `dest_column` -- FF declaration locations when known
- optional `sva_assertion_id` -- emitted only when `--emit-sva` successfully
  writes an assertion for this crossing. It is the first assertion label in
  the generated SVA file (2FF/3FF stage transfer takes precedence when both
  styles apply).
- optional `sva_assertion_ids` -- emitted when more than one assertion is
  written for the crossing; ordered stage-transfer, FIFO Gray encoding,
  req/ack ACK contract, then req/ack-data hold contract when applicable.
  Every label appears in the SVA file.

The FIFO Gray property is emitted only for a uniquely identified
`prim_fifo_async` `fifo_wptr_gray_q` or `fifo_rptr_gray_q` FF with a matching
`sync_wptr`/`sync_rptr` destination, width >= 2, known source clock, and
matching asynchronous active-low reset. It mirrors the primitive's one-bit
Gray-pointer transition assertion. It does not prove synchronizer safety,
metastability resolution, or protocol-level data stability. Other FIFO
records remain documentation-only unless an independent stage assertion
qualifies.

The `prim_sync_reqack` ACK-needs-REQ property is emitted only for a recognized
`src_req_q` FF crossing into the primitive's first `req_sync` stage, with
single-bit source/destination FFs, known destination clock, and matching
asynchronous active-low resets. A directed reset alias must connect the
primitive's `rst_dst_ni` port to the first synchronization FF. It mirrors the
primitive's destination-side
`dst_ack_i |-> dst_req_o` integration contract. It does not assert eventual
ACK, source request hold, data stability, or CDC safety; those require
additional caller and parameter assumptions.

The `prim_sync_reqack_data` hold property is emitted only for a connected
two-clock primitive with all ten clock, reset, handshake, and data ports
connected, matching integral port widths and directions, known positive
`Width`, boolean `DataSrc2Dst` and `EnReqStabA`, and `DataReg=0`. The
`DataSrc2Dst=1` property samples `data_i` on `clk_src_i` and requires a data
change to occur outside a pending request, or with ACK already visible. The
`DataSrc2Dst=0` property samples `data_o` on `clk_src_i` and checks its hold
window around `src_req_i && src_ack_o`, including two previous source-clock
samples. Both use `rst_src_ni` and mirror OpenTitan's primitive assertions;
they are caller obligations, not proof that data remains stable in every
clock window. Buffered (`DataReg=1`), partially connected, and generic
handshake instances get no hold assertion. JSON links the generated
`_data_hold_src2dst` or `_data_hold_dst2src` label on the corresponding
CAUTION crossing without changing its classification.

Generated SVA expressions use AST-declared signal/reset paths and scoped FF
clock paths. The standalone assertion module should be compiled alongside the
design top; successful elaboration does not establish simulation reachability
or prove CDC safety.

### Recognized `sync_type` values
- `none` -- no synchronizer recognized
- `two_ff` -- 2-FF synchronizer chain
- `three_ff` -- 3-FF synchronizer chain
- `gray_code` -- gray-coded multi-bit chain (per-bit shift sync)
- `johnson_counter` -- Johnson/twisted-ring shift register sync
- `mux_sync` -- multiplexer-based synchronizer with synced select
- `pulse_sync` -- toggle + 2-FF + XOR edge-detector pulse synchronizer
- `handshake` -- 4-phase req/ack handshake pair
- `async_fifo` -- gray-code asynchronous FIFO with synchronized
  read/write pointers

## Out of scope for Phase 1B freeze
- confidence levels
- unsupported construct summaries
