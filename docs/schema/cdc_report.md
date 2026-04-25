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
- `relationship` -- one of `asynchronous`, `inferred_asynchronous`,
  `synchronous_same`, `divided`, `phase_offset`,
  `physically_exclusive`, `logically_exclusive`, `gated`
- `rationale` -- human-readable string explaining why the relationship
  was inferred or asserted
- `timing_basis_ns` -- nullable number; the dest-clock period in
  nanoseconds when an SDC `create_clock` provided one, otherwise null

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
