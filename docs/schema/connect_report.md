# connect_report.json schema contract (stable-now)

This document records the **stable-now** contract frozen in Phase 1B.
Fields added later for confidence / rationale / unsupported constructs remain provisional
until analytical-trust milestones explicitly freeze them.

## Top-level keys
- `version`
- `top`
- `summary`
- `issues`
- `analysis`
- `connections`
- `bit_flow_gaps` (provisional)

## `summary`
- `connections_analyzed`
- `errors`
- `warnings`
- `info`
- `bit_flow_gap_count` (provisional)
- `bit_flow_gap_reasons` (provisional) — reason-to-count map whose values sum
  to `bit_flow_gap_count`
- `waived`

## `issues[*]`
- `type`
- optional `rule_id` (user-defined checker identifier)
- `severity`
- `port`
- `detail`
- optional `source`
- optional `dest`
- optional `file` (source path of the port declaration, emitted only when a
  trusted port location is available and the issue has no separate line)
- optional `line` (since v0.3.3, integer; emitted when known and non-zero)
- optional `column` (since v0.3.3, integer; emitted when known and non-zero)

## `analysis`
- `overall_score`
- `total_ports`
- `total_connections`
- `total_issues`
- `module_health`
- `coupling`
- `risks`

## `connections[*]`
- `source`
- `dest`
- `kind` -- `direct` when the extracted connection has exact structural
  evidence, including a sole unconditional blocking `always_comb` copy/select;
  `approximate` for conditional or unsupported procedural/heuristic dependencies. This is not
  a design-correctness verdict.
- optional `source_bits`, `dest_bits` -- `{low, high}` ordinal bit offsets within
  the respective ports for supported constant-slice (including indexed `+:` /
  `-:` selects whose base and width resolve during elaboration), constant-indexed integral
  unpacked-array or nested constant-indexed packed-array element/bit (including
  packed fields in constant-indexed unpacked elements), direct constant
  whole-interface integral-member slices (also paired with modport members or
  forwarded through direct interface-to-interface slice assignments),
  whole packed-array port, a sole unconditional `always_comb` copy/select,
  positional concatenation flow, a resolved ternary
  arm's positional may-flow, or a simple implicit integral width change or
  explicit size cast of a resolved signal or unsigned concatenation. Signed high
  bits can appear as repeated one-bit links from the
  source sign bit. For an `approximate` whole-interface computed or guard read,
  ranges bound the accessed member lanes but do not prove expression-level
  bit correspondence or branch reachability. For an `approximate` ternary
  assignment, ranges map each possible arm positionally, not prove which arm
  is selected. Guarded `if/case` copies of resolved integral nets can likewise
  provide positional `approximate` ranges while retaining a range-free may-flow
  row for transitive paths through unsupported conversions. A compile-time-known
  `if`, `case`, `casez`, or `casex` contributes only its selected branch; runtime selectors
  retain may-flow from all possible branches. Ranges are omitted when even the
  accessed lanes are unknown.
- `status`

A full-width exact `direct` row replaces a redundant range-free `direct` row
for the same source/destination pair. Partial exact rows and `approximate`
rows remain separate; the connection count is report rows, not unique port
pairs or verified physical paths.
Runtime-indexed element reads and combinational procedural writes may yield range-free `approximate` rows
from every structurally bound candidate with the same declaring scope,
compatible fixed indices, and member names. They are possible dependencies,
not evidence that a particular lane is selected at runtime.
An indexed part-select with a runtime base can similarly retain a range-free
`approximate` dependency on the underlying vector; it never names an exact
selected lane.

## `bit_flow_gaps[*]` (provisional)

At most four source-ordered examples per reason (32 total) of assignments
containing a concatenation or bit selection whose exact positional bit mapping
is incomplete. Signal-free tie-offs are excluded. The summary count includes
all qualifying elaborated assignment instances, not just the examples. A gap
may still have conservative approximate connection edges; the count is not
the number of missing edges or a recall estimate.

- `scope` — instance/generate scope containing the assignment
- `reason` — `width_limit`, `width_mismatch`, `missing_type`,
  `width_changing_conversion`, `state_changing_conversion`, `unflattenable_expression`,
  `unresolved_destination_range`, `unresolved_source_range`, or
  `nonstructural_source`
- `lhs_width`, `rhs_width` — elaborated assignment widths
- optional `file`, `line` — assignment location, when available

The positional graph currently caps individual assignments at 4,096 bits.
This list does not enumerate every unsupported RTL construct; ordinary
arithmetic and control-flow dependencies outside these bit-flow candidates
can still be approximate or absent.

## Out of scope for Phase 1B freeze
- confidence levels
- heuristic / rationale annotations
- complete unsupported construct summaries
