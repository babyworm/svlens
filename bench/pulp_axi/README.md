# Optional PULP AXI-Lite 2x2 probe

This benchmark uses a small repository-owned fixture with upstream RTL pinned
to PULP AXI `70b8e54fd460e3308e58be596ceb3566a6e3576e` and common_cells
`e73baaec2ca665cd80c3c384e9258e35242b829c`. These clones are needed
only when this optional probe runs; they are not build dependencies.
The fixture's two managers have independent runtime AW/AR addresses, request
data, and valid/ready inputs. Both address-map regions can therefore be
selected by either manager. The two subordinates likewise have independent
runtime responses. This does not guarantee any particular arbitration
outcome, but avoids treating an unreachable fixed-address route as evidence.

With clean clones at those commits and a built `svlens` binary:

```bash
python3 -m unittest discover -s bench/pulp_axi -p 'test_*.py'
python3 bench/pulp_axi/run.py \
  --axi-root /path/to/axi \
  --common-cells-root /path/to/common_cells \
  --svlens build/svlens \
  --output /path/to/new/report-directory
```

The runner refuses a stale `connect_report.json`, verifies both commit pins
and clean tracked sources, checks the fixture SHA-256, then writes
`connect_report.json` and `evaluation.json`. `svlens conn` can exit nonzero for
reported issues; the evaluator requires zero analysis errors and complete
connection rows.

If the repository's optional Verilator tool is installed, exercise both
address regions and the response return path as well:

```bash
python3 bench/pulp_axi/run_sim.py \
  --axi-root /path/to/axi \
  --common-cells-root /path/to/common_cells \
  --output /path/to/new/simulation-directory
```

The simulation completes six cases: both manager→subordinate assignments,
address boundaries `0`, `127`, `128`, and `255`, and both managers contending
for each subordinate. Each manager sends one write and one read per case,
for 12 completed writes and 12 completed reads. The scoreboard checks AW/AR
addresses and protection, W data and strobes, and B/R responses at actual
handshakes. It rejects wrong routes, duplicate transfers, and timeouts without
requiring a specific arbitration winner. All five channels must encounter
backpressure during the run. A second execution corrupts R data and must fail
at the response assertion, ensuring that disabled or ineffective assertions
cannot pass the benchmark.

The testbench and fixture hashes are pinned. The runner refuses a nonempty
output directory, retains `build.log`, `simulation.log`, and
`negative-control.log`, and writes `simulation.json` only after both executions
behave as expected. This is bounded behavioral evidence; it does not exhaust
arbitration schedules, resets during traffic, or protocol behavior.

The checked source-derived frame is deliberately bounded:

- 76 exact, correctly directed AXI-Lite modport boundary relations: 19
  signals × 2 lanes × manager/subordinate sides, with full ordinal ranges.
- Four exact manager AW/AR address paths to same-lane internal decoder inputs;
  wrong-lane address paths are forbidden.
- Four positional `approximate` response candidates from both master muxes
  to both slave demuxes. The checker does not treat a runtime-selected response
  as a `direct` route.
- 24 positional `approximate` request-data candidates: AW address/protection,
  W data/strobe, and AR address/protection from either manager to either
  subordinate. The fully connected crossbar allows each candidate; the
  checker does not assert that it wins arbitration.

The evaluator rejects missing, duplicated, downgraded, wrong-width, or
wrong-lane rows. It does not prove arbitration decisions, protocol behavior,
CDC safety, or whole-crossbar precision/recall. In particular, the entire
report contains additional rows that this 108-pair frame has not adjudicated.

The existing `opentitan-benchmark` workflow runs this as a separate job on
manual dispatch and uploads a `pulp-axi-benchmark` artifact. Its weekly
scheduled run stays OpenTitan-only. The manual job also runs the existing
optional Verilator tool and includes `simulation.json` in its artifact.
The `pulp-axi-simulation` artifact retains the simulation logs even on failure.
