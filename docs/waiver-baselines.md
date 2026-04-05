# Waiver and baseline guidance for large SoC designs

`svlens` is intentionally positioned as a **pre-signoff structural analysis** tool.
On large SoC-scale RTL, the right workflow is usually:

1. run the tool on the full elaborated design,
2. separate tool noise from real design risk,
3. record reviewed intent in waiver files,
4. use baselines / diffs to keep CI focused on new changes.

This document captures the current recommended workflow, plus the replay
results from two large open SoC designs that were used to tune the tool.

---

## Scope and goals

This guide covers:

- **connectivity baselines** for `svlens conn`
- **CDC waivers** for `svlens cdc`
- large-design triage on designs with many expected warnings
- how to keep CI actionable after the first adoption wave

It does **not** redefine severity semantics:

- `ERROR` / `VIOLATION` still deserve review first
- `WARN` / `CAUTION` often need project context
- `INFO` should usually be tracked but not block CI

---

## What the large-SoC replay showed

The following large designs were replayed against current `main` after the
false-alarm reduction work landed:

| Design | Top | Mode | Result |
|---|---|---|---|
| Cheshire | `cheshire_soc` | `conn` | `182` connections, `0` errors, `238` warnings |
| Cheshire | `cheshire_soc` | `cdc` | `0` violations, `7` cautions, `2` info |
| PULPissimo | `pulpissimo` | `cdc` | `0` violations, `16` cautions, `4` info |

### Key takeaways

- The highest-confidence false alarms that were observed during early SoC
  replay are now handled in-tool:
  - optional `id_queue`-style `exists_*` inputs disabled by `exists_req_i == 0`
  - intentional `isochronous_4phase_handshake` crossings
  - intentional `syncreg` crossings
  - combinational-only debug/JTAG mux logic being mistaken for FF state
- Large SoC designs can still produce **many warnings/cautions** even when
  hard failures are gone.
- That makes **reviewed waivers + baselines** essential for day-to-day CI use.

---

## Recommended adoption sequence

### 1. First connectivity snapshot

Generate a machine-readable report for the whole design:

```bash
svlens conn -F filelist.f --top soc_top --format json -o reports/conn
```

If your design intentionally contains many no-connect or tie-off ports, try:

```bash
svlens conn -F filelist.f --top soc_top \
  --ignore-nc --ignore-tie-off \
  --format json -o reports/conn
```

### 2. First CDC snapshot

Prefer JSON plus your best available clock intent:

```bash
svlens cdc -F filelist.f --top soc_top --format json -o reports/cdc
```

With timing intent:

```bash
svlens cdc -F filelist.f --top soc_top --sdc clocks.sdc --format json -o reports/cdc
```

or:

```bash
svlens cdc -F filelist.f --top soc_top --clock-yaml clock_domains.yaml --format json -o reports/cdc
```

### 3. Review before waiving

Use this order:

1. `ERROR` / `VIOLATION`
2. repeated `WARN` / `CAUTION` families
3. remaining noisy-but-intentional project conventions

### 4. Freeze reviewed intent

- connectivity: record reviewed patterns in a conn waiver file
- CDC: use a CDC waiver file only after confirming the crossing is intentional
- connectivity CI drift: compare new reports against a reviewed baseline

---

## Connectivity baseline strategy

`svlens conn` supports both **waivers** and **JSON diffing**.

### Use waivers for stable intent

Good candidates:

- intentional debug outputs left open
- well-known test-only ports
- permanently unused ports in validated wrappers

### Use diff baselines for regression control

Good candidates:

- block-level reports already reviewed once
- top-level reports with many legacy warnings
- CI flows where the goal is “no new connectivity regressions”

Example:

```bash
svlens conn -F filelist.f --top soc_top --format json -o reports/conn
cp reports/conn/connect_report.json baseline/connect_report.json

svlens conn -F filelist.f --top soc_top \
  --diff baseline/connect_report.json \
  --format json -o reports/conn-next
```

### Suggested connectivity rollout policy

| Stage | Policy |
|---|---|
| Initial bring-up | Review all `ERROR`, inspect major warning families |
| First CI gate | Fail on active `ERROR`, allow reviewed legacy warnings |
| Mature CI gate | Use `--diff` against reviewed JSON baseline |

---

## Connectivity waiver format

Connectivity waivers use glob-style path matching:

```yaml
waivers:
  - pattern: "*.o_debug*"
    type: DANGLING_OUTPUT
    reason: "Debug outputs are intentionally left open at SoC top"

  - pattern: "top.u_test_*"
    type: "*"
    reason: "Temporary test harness block"

  - source: "top.u_legacy_block.i_unused_cfg"
    type: UNDRIVEN_INPUT
    reason: "Reviewed legacy optional port; wrapper leaves it intentionally open"
```

### Connectivity waiver advice

- Prefer **narrow patterns** over broad `"*"` waivers.
- Prefer `source:` exact matches for one-off exceptions.
- Do not waive an entire subsystem until you have reviewed the individual issue
  family and know why it is safe.

---

## CDC waiver strategy

Use CDC waivers for **reviewed intentional crossings**, not for “unknown but
probably okay” paths.

Good candidates:

- debug-domain crossings that are architecturally intentional
- reset / bring-up sequences with known synchronization wrappers
- top-level lab / DFT structures documented by the owning team

Bad candidates:

- unsynchronized raw data crossings that were never architecturally reviewed
- crossings whose severity changed only because clock intent is incomplete
- broad pattern waivers used as a substitute for SDC / clock YAML

---

## CDC waiver format

CDC waivers support either an exact crossing string or a simple prefix pattern.

Example:

```yaml
waivers:
  - id: WAIVE-001
    crossing: "top.u_dbg.req_sync_q -> top.u_core.req_q"
    reason: "Reviewed debug synchronizer path"
    owner: "soc-infra"
    date: "2026-04-05"

  - id: WAIVE-002
    pattern: "top.u_dbg."
    reason: "Reviewed debug-domain crossings in bring-up logic"
    owner: "soc-infra"
```

### CDC waiver advice

- Prefer `crossing:` for specific reviewed paths.
- Use `pattern:` only for tightly-scoped ownership boundaries.
- Keep `owner` and `date` populated so waivers can be re-reviewed later.

You can generate a starting template with:

```bash
svlens cdc -F filelist.f --top soc_top --format waiver -o reports/cdc
```

---

## Suggested CI policy for large SoCs

### Connectivity in CI

Recommended progression:

1. fail on active `ERROR`
2. store reviewed baseline JSON
3. run `--diff` in CI
4. only page humans for new connectivity regressions

### CDC in CI

Recommended progression:

1. fail on active `VIOLATION`
2. keep reviewed intentional paths in a CDC waiver file
3. keep clock intent (`--sdc` or `--clock-yaml`) under version control
4. track `CAUTION` trends even if they do not block merges

---

## Interpreting the Cheshire / PULPissimo replay

These two designs are useful references for what “healthy but noisy” can look
like on a large open SoC:

- **Cheshire connectivity** still has many warnings after hard errors were
  removed. That is a baseline-management problem, not necessarily a tool
  failure.
- **Cheshire / PULPissimo CDC** now show reviewed non-zero `CAUTION` / `INFO`
  counts but no remaining `VIOLATION` in the replayed configuration.

That means the practical next step for similar designs is usually **waiver and
baseline discipline**, not more blanket suppression.

---

## Practical rules of thumb

- Review every new `ERROR` / `VIOLATION` as potentially real.
- Keep waivers **specific, owned, and dated**.
- Prefer **clock intent** over waiver growth for CDC.
- Prefer **diff baselines** over giant wildcard waivers for connectivity.
- Re-run SoC snapshots after upgrading `svlens`; a tool improvement may let you
  delete legacy waivers.

---

## Related references

- CLI contract: [`docs/cli-help.md`](cli-help.md)
- install / offline usage: [`docs/install.md`](install.md)
- JSON report contracts:
  - [`docs/schema/connect_report.md`](schema/connect_report.md)
  - [`docs/schema/cdc_report.md`](schema/cdc_report.md)
  - [`docs/schema/svlens_summary.md`](schema/svlens_summary.md)
