# Optional PULP AXI-Lite 2x2 probe

This benchmark uses a small repository-owned fixture with upstream RTL pinned
to PULP AXI `70b8e54fd460e3308e58be596ceb3566a6e3576e` and common_cells
`e73baaec2ca665cd80c3c384e9258e35242b829c`. These clones are needed
only when this optional probe runs; they are not build dependencies.

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
and clean tracked sources, then writes `connect_report.json` and
`evaluation.json`. `svlens conn` can exit nonzero for reported issues; the
evaluator requires zero analysis errors and complete connection rows.

The checked source-derived frame is deliberately bounded:

- 76 exact, correctly directed AXI-Lite modport boundary relations: 19
  signals × 2 lanes × manager/subordinate sides, with full ordinal ranges.
- Four exact manager AW/AR address paths to same-lane internal decoder inputs;
  wrong-lane address paths are forbidden.
- Four positional `approximate` response candidates from both master muxes
  to both slave demuxes. The checker does not treat a runtime-selected response
  as a `direct` route.

The evaluator rejects missing, duplicated, downgraded, wrong-width, or
wrong-lane rows. It does not prove arbitration decisions, protocol behavior,
CDC safety, or whole-crossbar precision/recall. In particular, the entire
report contains additional rows that this 84-pair frame has not adjudicated.

The existing `opentitan-benchmark` workflow runs this as a separate job on
manual dispatch and uploads a `pulp-axi-benchmark` artifact. Its weekly
scheduled run stays OpenTitan-only.
