# cdc_report.json schema contract (stable-now)

This document records the **stable-now** Phase 1B contract.
Additional confidence / rationale fields remain provisional until CDC analytical-trust
milestones freeze them.

## Top-level keys
- `summary`
- `domains`
- `crossings`

## `summary`
- `violations`
- `cautions`
- `conventions`
- `info`
- `waived`

## `domains[*]`
- `name`
- `source`

## `crossings[*]`
- `id`
- `source`
- `dest`
- `source_domain`
- `dest_domain`
- `path`
- `category`
- `severity`
- `sync_type`
- `rule`
- `recommendation`

## Out of scope for Phase 1B freeze
- confidence levels
- timing rationale annotations
- unsupported construct summaries
