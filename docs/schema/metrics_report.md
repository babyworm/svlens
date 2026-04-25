# metrics_report.json schema contract (stable-now)

This document records the **stable-now** contract for `svlens metrics` output.
The schema defines deterministic, quantitative guardrails for RTL complexity
based on backward transformation cones and repeated bit-lane normalization.

## Determinism rules

- `roots[]` ordering: `root_kind` ascending, then `root_id` ascending
- `normalization.groups[]` ordering: `signature`, `representative_width`, `multiplicity`
- `ff_paths[]` ordering: `source_ff`, `dest_ff`
- Same input + same options => identical JSON output

## Top-level keys
- `version` (schema version, e.g. `"1.1"`)
- `tool_version` (svlens version, e.g. `"0.2.8"`)
- `top`
- `summary`
- `analysis`
- `roots`
- `ff_paths`
- `normalization`
- `unsupported`

## `summary`
- `outputs_analyzed`
- `ff_d_roots_analyzed`
- `cones_analyzed`
- `approximate_cones`
- `unsupported_count`

## `analysis`
- `raw_transform_count`
- `normalized_transform_count`
- `repeated_lane_groups`
- `ff_to_ff_paths`
- `max_output_cone`
- `max_ffd_cone`

## `roots[*]`
- `root_id`
- `root_kind`  (`output` | `ff_d`)
- `raw_node_count`
- `logic_depth_est`
- `normalized_transform_count`
- `repeated_lane_group_count`
- `source_inputs`
- `source_ffs`
- `approximate`

## `ff_paths[*]`
- `source_ff`
- `dest_ff`
- `has_comb_logic`
- `comb_signal_count`
- `normalized_comb_count`
- `sync_type`
- `path`
- `approximate`
- `provenance_level`  (`hint_only` | `partial_slice` | `provenance_backed`)

## `normalization`
- `enabled`
- `lane_min_width`
- `groups[*]`
  - `signature`
  - `multiplicity`
  - `representative_width`
  - `collapsed_from`

## `unsupported[*]`
- `kind`
- `count`
- optional `examples`

## Provenance level semantics

| Level | Meaning |
|---|---|
| `hint_only` | Path derived from CDC FFEdge/fanin hints only; no provenance extraction performed |
| `partial_slice` | Some transforms extracted, but cone is incomplete due to unsupported constructs |
| `provenance_backed` | Full backward cone extracted with provenance-preserving transform graph |

## Out of scope for MVP
- per-module aggregate scoring / CI threshold profiles
- full procedural semantics (general `always_comb` blocks beyond case/for)
