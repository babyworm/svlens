# svlens Implementation Status

Updated: 2026-10-08

## Summary

The repository has one user-facing executable, `svlens`, with these modes:

- `svlens conn` — connectivity and structural checks
- `svlens cdc` — structural clock-domain crossing checks
- `svlens metrics` — RTL transformation complexity
- `svlens all` — runs all three modes over a shared compilation session

`svlens both` remains a backward-compatible alias for `all`. The previous
standalone binaries are no longer the primary interface.

## Implemented Architecture

### Common layer

- `svlens-common-lib`
  - common CLI helper logic
  - shared compilation frontend
  - shared filelist expansion

Key files:

- [`src/CommonCli.h`](../src/CommonCli.h)
- [`src/CommonCli.cpp`](../src/CommonCli.cpp)
- [`src/CompilationSession.h`](../src/CompilationSession.h)
- [`src/CompilationSession.cpp`](../src/CompilationSession.cpp)

### Connectivity layer

- `svlens-conn-lib`

Key files:

- [`src/ConnRunner.h`](../src/ConnRunner.h)
- [`src/ConnRunner.cpp`](../src/ConnRunner.cpp)
- [`src/ConnRunnerUtils.h`](../src/ConnRunnerUtils.h)
- [`src/ConnRunnerUtils.cpp`](../src/ConnRunnerUtils.cpp)

### CDC layer

- `svlens-cdc-lib`

Key files:

- [`src/CdcRunner.h`](../src/CdcRunner.h)
- [`src/CdcRunner.cpp`](../src/CdcRunner.cpp)
- [`src/CdcRunnerUtils.h`](../src/CdcRunnerUtils.h)
- [`src/CdcRunnerUtils.cpp`](../src/CdcRunnerUtils.cpp)
- vendored CDC implementation under [`src/cdc/`](../src/cdc/)
- vendored public headers under [`include/sv-cdccheck/`](../include/sv-cdccheck/)

### Metrics layer

- `svlens-metrics-lib`
- transformation extraction, cone analysis, normalization, and baseline diffing

Key files:

- [`src/MetricsRunner.cpp`](../src/MetricsRunner.cpp)
- [`src/metrics/TransformExtractor.cpp`](../src/metrics/TransformExtractor.cpp)
- [`src/metrics/ConeAnalyzer.cpp`](../src/metrics/ConeAnalyzer.cpp)

### Unified orchestration

- [`src/svlens_main.cpp`](../src/svlens_main.cpp)

`svlens all` runs `conn`, `cdc`, and `metrics` over one shared
`CompilationSession`, writes split output directories, and emits
`svlens_summary.json`. The summary contains exit codes, artifact paths, and
exact-signal links between conn issues and CDC crossings when both JSON
reports are available.

## Testing Status

The repository now includes:

- legacy connectivity unit tests
- unified CLI integration tests
- CDC utility/unit tests
- CDC pipeline tests
- CDC runner tests
- CDC golden integration tests
- imported CDC upstream tests
- metrics feature and determinism tests

Representative scripts:

- [`tests/test_integration.sh`](../tests/test_integration.sh)
- [`tests/test_svlens_integration.sh`](../tests/test_svlens_integration.sh)
- [`tests/test_cdc_golden.sh`](../tests/test_cdc_golden.sh)
- [`tests/test_metrics_features.sh`](../tests/test_metrics_features.sh)

## Current Validation Result

The full local regression on 2026-10-08 passed:

- `cmake --build build -j8`
- `ctest --test-dir build --output-on-failure -j8`
- `ctest --test-dir build-ubsan --output-on-failure -j4`
- **735 / 735 tests passing** in both builds

The shared inline-SV test helper now owns the slang driver for the full AST
lifetime. This removes a dangling-source dependency that could corrupt
hierarchical names and make CDC regressions depend on allocator behavior.
The full ASan regression also passes **735 / 735** when svlens is linked to a
separately built slang v10 at the same pinned commit with
`SLANG_USE_MIMALLOC=OFF`. The default installed slang exports mimalloc and the
same ASan test binary crashes inside `mi_free_block_delayed_mt` on standalone
DOT/JSON report tests. The opt-in prefix flow is documented in
[`docs/install.md`](install.md#addresssanitizer-validation) and configured as
a full-test CI job (external CI execution has not yet been observed here);
the default dependency install remains unchanged.

The OpenTitan benchmark parser/evaluator suite passes 32/32 Python tests and
Ruff checks. Pinned AES and full-SoC SVA elaborate with their RTL (0 errors;
the SoC's 12 implicit-conversion warnings also occur without SVA). The
benchmark gates AES's data-hold reference (1/1), four SoC 2FF/FIFO/req-ack
references (4/4), all 320 SoC assertion labels against JSON IDs (320/320),
and unchanged CDC classifications. These are structural emission and
elaboration checks, not protocol proofs.

The former 464-test figure predates metrics and is no longer representative.

## Packaging / CI

Primary build/install flow:

- [`Makefile`](../Makefile)
- [`.github/workflows/ci.yml`](../.github/workflows/ci.yml)

Dependency bootstrap:

- [`scripts/setup-deps.sh`](../scripts/setup-deps.sh)

## Current Boundaries

- Modport members have per-signal edges and width checking; directly used
  whole-interface members gain direction-aware approximate edges. Direct
  constant bit/part selects of an integral member now preserve only the
  overlapping ordinal bits, including modport-to-whole-interface pairings;
  direct slice assignments can forward those bits across two distinct
  interface instances without conflating unrelated instances. Constant member
  selects inside computed reads or procedural guards retain only the selected
  input bits, but stay approximate; unsupported expressions remain whole-member
  may-flow. `always_comb`
  and `always @*` if/case/ternary glue add directed dependencies. A sole
  unconditional blocking `always_comb` copy or constant select preserves direct
  bit lanes; guarded, overwritten, compound, and legacy `always @*` assignments
  stay approximate. Resolved guarded `if/case` copies additionally expose
  positional approximate lanes, while their coarse may-flow remains available
  across later unsupported casts. Compile-time-known `if`, `case`, `casez`, and `casex`
  prune inactive branches in both procedural dependencies and whole-interface
  member-use inference; runtime selectors stay conservative. Generated
  if/for member uses and declaring-scope procedural aliases are covered by
  small positive/negative fixtures, along with nested forwarding, AXI-lite
  modports, and fixed/genvar-indexed parameterized interface-array elements.
  Direct constant slices, elaborated indexed `+:` / `-:` part-selects (including
  generated banks and modport members), constant-indexed integral unpacked-array elements,
  nested constant-indexed packed-array elements/bits (including packed fields
  inside constant-indexed unpacked elements), whole packed-array ports, and positional
  concatenations up to 4,096 bits include ordinal source/destination ranges
  through simple assignment chains. Directly resolved simple implicit width
  changes and explicit size casts now map retained low bits and repeated signed
  high bits; unsigned concatenations inside size casts map retained operand lanes.
  Width-changing type casts that also change signedness, casts changing
  two-state/four-state representation, and computed width conversions remain approximate; non-identity conversions
  no longer produce a contradictory range-free `direct` alias. Arithmetic leaves
  and branch controls remain approximate. Pinned slang v10 rejects nonconstant
  interface-instance array indexes during elaboration. Nonintegral array
  elements and full branch-sensitive/cast dataflow remain incomplete.
  Runtime-indexed part-select starts remain range-free approximate may-flow;
  runtime-indexed element reads and combinational procedural writes now connect to structurally
  bound candidate elements as range-free approximate may-flow edges; fixed
  index and member mismatches are excluded, but the selected lane and branch
  reachability are not proven. Equal-width ternary arms with resolved ranges
  add positional `approximate` may-flow for each runtime branch; a known
  constant condition uses only its selected arm and does not mark an
  unselected whole-interface member as read. This does not prove selector
  reachability. Connection JSON labels every edge `direct` or `approximate` and
  counts unmapped positional assignment instances by reason, retaining up to
  four source-located examples per reason. This gap count is not a measurement
  of missing edges or recall.
- CDC uses SDC clock relationships and exceptions. Finite positive periods
  propagate through uniquely resolved SDC generated-clock divide/multiply
  chains and recognized unconditional or reset-only scoped RTL toggle
  dividers. Enabled toggles need explicit SDC timing. Invalid, ambiguous, or
  cyclic generated masters are not linked. Periods add a conservative
  fast-to-slow `Ac_cdc08` data-stability review hint but do not prove capture
  timing or change structural categories. Conditional wide-bus FF assignments
  now expose their guard signals and receive an `Ac_cdc08` review hint without
  downgrading an async violation. `Ac_cdc12` is heuristic;
  `Ac_cdc10` detects registered data used as an undeclared clock when a unique
  FF drives the clock origin through direct assignments or directed ports;
  unrelated same-named FFs are not guessed as drivers. External data-named clock
  inputs remain naming hints. Instance-scoped auto-detected clocks no longer
  merge across siblings by leaf name. Full reset-domain crossing analysis is
  absent. CDC JSON exposes FF reset usage by signal and destination domain as
  a first topology inventory. `Ac_cdc06`
  now requires a unique FF reset driver through direct assignments, port
  aliases, exact hierarchical references, or one-bit inversion, including a
  sole unconditional `always_comb` assignment and cross-instance paths.
  Clocked packed-struct member writes now create member-path FFs with their
  declared widths, and member reads can feed CDC FF edges; scalar reset-named
  struct output fields can be followed through directed ports. A constant bit
  of a packed reset vector feeding a scalar reset port can retain its unique
  aggregate FF driver. Separately registered scalar output bits in one reset
  vector now retain their own indexed FF source without merging sibling bits;
  dynamic selectors and duplicate FF paths do not guess one. Scan-mode reset
  muxes remain nontransparent, but `reset_usage[*].conditional_muxes` now
  inventories reachable, signature-checked two-input mux output/input/select
  paths without choosing a branch or FF driver. A mux ancestor vetoes unique
  FF provenance even when another direct alias reaches an FF; a direct route
  through an indexed reset-struct field can still retain its scalar FF driver.
  Immediate mux input nets and constant-selected elements are distinguished
  from computed-expression may-dependencies; non-identity width/state casts
  no longer form transparent CDC clock aliases.
  A mux selector fixed to 0/1 at elaboration follows only that input and can
  retain a unique FF source only when that chosen branch independently
  resolves to one; another alias does not substitute for an external branch.
  Runtime or X/Z selectors, competing immediate drivers, and mux cycles
  remain unresolved. None of the 29 distinct mux outputs in the pinned SoC has an
  elaboration-time fixed selector, so its CDC classifications are unchanged.
  The source-only SoC run sees
  5,319 reset paths with candidates across 29 distinct mux outputs (zero
  truncated traces); 5,288 paths expose branch FF candidates spanning 21
  distinct FFs. HMAC's input-0 branch reaches
  `u_0_sys.u_sync_2.q_o` in `clk_main_i`, but its mux output remains
  driver-unresolved. Conditional or multiply
  assigned procedural resets, ambiguous driver/inversion-polarity paths, and name-only
  matches are left unresolved. `reset_usage` includes the driver FF and source
  domain when every sink for that reset path agrees, plus `driver_inverted`
  when inversion parity agrees (false for direct/even, true for odd). A data
  2FF path does not waive an
  asynchronous reset use.
  A one-bit `~` or `!` on a mux input can supply a branch FF candidate;
  a fixed selector can retain the inversion parity in `driver_inverted`.
  Candidate FF entries now carry mux-input-relative inversion parity, with
  direct/inverted reconvergence reported as ambiguous rather than guessed.
  A wider inversion is not promoted to a reset alias. The pinned SoC counts
  and crossing classifications are unchanged by this local fixture coverage.
  Indexed reset-array branches and nontransparent reset logic remain incomplete.
  `Ac_cdc09` now reports the resolved hierarchical clock-net path, including
  direct port renames, so a same-named net elsewhere is not confused with the
  source in JSON.
  A matching SDC `set_false_path` now appears as `sdc_false_path` timing
  context without downgrading the crossing; an unsynchronized path remains a
  VIOLATION. Generated `cdc_constraints.sdc` is a review-only comment template
  with no executable false paths or guessed maximum delays.
  Declared physically/logically exclusive clock groups retain their
  relationship labels, but an unsynchronized crossing remains a CAUTION
  requiring mode-transition review; `--strict` makes that a failing gate.
  Static `[get_clocks {name ...}]` groups resolve to their clock names, and
  `-include_generated_clocks` expands literal roots through verified generated
  master chains, including forward-declared SDC children. Missing/ambiguous
  names or overlapping groups skip the relationship with a CLI warning;
  other Tcl options, wildcards, and dynamic selectors skip the parsed command.
  A unique, valid clock-pair `set_max_delay` is reported as a declared
  constraint, not measured path delay or a CDC waiver. Duplicate limits and
  clock-name collisions are marked ambiguous; invalid or pin/path-specific
  forms are skipped with a warning. Exception precedence remains unverified.
  Unsynchronized multi-bit async crossings still fail, but their recommendation
  now asks for coherent bus transfer rather than an independent 2FF per bit.
  Single-bit async crossings still fail too; their recommendation distinguishes
  a stable level from a pulse/event and requires clock-relationship review
  before choosing 2FF or a pulse/toggle transfer protocol.
- CDC source resolution no longer connects unrelated sibling FFs by matching
  a leaf name; a connected input port's actual net takes precedence over an
  inherited same-named wire.
- Direct assignments and directed module ports now preserve top-clock lineage
  across transparent buffers and clock-named struct output fields, regardless
  of instance declaration order. CDC JSON separately records root provenance
  through recognized one-input gates/dividers without merging their domains.
  FFs within generated scopes now prefer the nearest scoped clock over a global
  same-named source; an SDC generated clock at a child output pin follows its
  directed port connection, without applying to unrelated same-named FFs.
  Direct aliases and port connections inside instantiated generate scopes now
  use declaration paths for both root lineage and the FF's active clock net;
  inactive branches are skipped. Conditional muxes and unsupported clock cells remain
  incomplete; root equality must not be inferred as safe CDC.
- A connected `prim_sync_reqack_data` port signature now exposes both clock
  boundaries as CAUTION records without claiming the primitive is safe.
- `--emit-sva` produces covers for eligible unsynchronized crossings and
  stage-transfer assertions for unambiguous 2FF/3FF chains. A verified
  `prim_fifo_async` Gray pointer can add a source-clock one-bit transition
  assertion, linked alongside a stage assertion in JSON when both exist.
  A verified `prim_sync_reqack` request path can add the destination
  ACK-requires-REQ contract assertion. A fully connected, parameter-checked
  `prim_sync_reqack_data` with `DataReg=0` can add its direction-specific
  source-clock data-hold assertion, linked to the CAUTION crossing in JSON.
  Generic handshake data-stability/liveness and FIFO protocol assertions
  remain unimplemented. FF, reset, and
  clock paths in generated SVA now use AST declaration scopes; fixture tests
  compile the SVA module with its RTL, including a generate-array case.
- Metrics handles several procedural forms and approximate function calls;
  roots report maximum graph fanout and an uncalibrated gate-cost proxy.
  Calibrated cell estimates and video-specific classification remain
  unimplemented.
- Conn and CDC waivers use separate YAML formats. Both accept a scoped
  `// svlens: waive ... reason: ...` source comment; CDC requires a destination
  FF path. `--user-rules` registers YAML name checkers, but
  arbitrary AST/plugin registration remains absent.
- An installable CLI-backed Python API, source-backed VS Code conn/CDC
  diagnostics, and SARIF/PR-summary converters exist. In-process pybind11
  bindings and changed-file incremental analysis remain absent.
- Conn HTML has an interactive graph with search and port expansion. CDC HTML
  now supports module/category filtering and a selected crossing trace;
  a shared cross-mode trace view remains absent.
- The [latest OpenTitan benchmark snapshot](benchmarks/opentitan-2026-10-08.md) records
  semantically elaborated local results, and a scheduled/on-demand workflow
  uploads raw reports. The generator substitutes generic OpenTitan primitives
  for source-only analysis. Four manually checked SoC FIFO crossings and one
  power-manager bundled-data path are present with expected roots (5/5);
  the power-manager `VIOLATION` category is also pinned (1/1), while path
  presence does not prove the data hold interval. Three SoC CDC guidance probes
  pin review-oriented recommendations for the two SPI paths and the
  power-manager path (3/3), without certifying safety. HMAC has three checked
  connectivity paths (3/3), including two runtime FIFO mux influences, and
  two selected absent-path probes (2/2). Six SoC EDN packed-array lanes, two AES
  SubBytes-to-ShiftRows share paths, and five generated AES PRNG seed lanes
  are checked for exact direct bit ranges (13/13); the seven AES paths are
  also checked in the standalone AES report (7/7).
  Two OTP response-valid sources are checked as possible dynamic-index paths,
  bringing the bounded SoC connectivity references to 10/10. The AES
  `crypt_q` feedback through sparse-signal checking to `crypt_d` adds one
  transitive approximate reference; with the five PRNG lanes, the current
  SoC connectivity total is 16/16.
  Five manually checked SoC absent-path probes (and two standalone AES probes)
  also pass; the evaluator requires both endpoints to occur elsewhere in the
  report. A separate benchmark-only period projection from the pinned ASIC
  SDC gives all four base-clock domains their expected `period_ns` values
  (4/4), and 157/243 SoC crossings a reported 4750 ns timing basis while
  preserving every structural classification; the power-manager path and
  projected root names match 1/1. It does not establish the divided IO
  capture period or data hold time. These selected examples do not measure
  whole-design precision.
  Separately, the older topology-only domain-pair
  probes show 0/6 local and 2/6 root-provenance pairs; their four absent pairs
  are not established false negatives. Seventy-three of 243 crossing records
  involve an `auto_port_*` domain, and 23/243 lack at least one root label;
  some are mode-dependent SPI/JTAG mux clocks and must not be assigned a root
  by name alone. Mixed constant packed/unpacked mapping adds 215 exact SoC
  range rows before 53 redundant full-width coarse direct rows are removed,
  and reduces reported bit-flow gaps by 1,492 relative to that older local
  binary. Indexed generated-bank part-selects then add 45 direct SoC rows and
  reduce recorded gaps by another 802; five AES PRNG lanes are manually
  adjudicated in both hierarchies. Other new rows remain unlabeled.
  A deterministic four-stratum sample of 20 SoC connection rows now has 20
  source-backed confirmations and zero contradictions or unresolved rows. The
  lockstep buffer bit index was independently checked by summing 30 operand
  widths. The evaluator gates annotation freshness and contradictions;
  the sample is too small and correlated to estimate whole-SoC precision or
  recall. The [2026-09-26 snapshot](benchmarks/opentitan-2026-09-26.md) preserves the
  earlier baseline before direct clock-alias propagation.

See [`README.md`](../README.md) for current user-facing limits and
[`docs/design/feature-roadmap.md`](design/feature-roadmap.md) for planned work.
