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

## `summary`
- `connections_analyzed`
- `errors`
- `warnings`
- `info`
- `waived`

## `issues[*]`
- `type`
- `severity`
- `port`
- `detail`
- optional `source`
- optional `dest`

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
- `status`

## Out of scope for Phase 1B freeze
- confidence levels
- heuristic / rationale annotations
- unsupported construct summaries
