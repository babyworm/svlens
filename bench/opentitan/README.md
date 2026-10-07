# OpenTitan Benchmark for svlens

Runs svlens against real OpenTitan RTL and records observed findings and wall
time. `domain_pair_probes` are topology questions, not verified signal-level
crossings; their presence count is not CDC recall or precision. Four manually
checked SoC FIFO crossings gate exact source/destination and top-clock root
labels. One power-manager cause-bus path also gates source/destination and
root labels and its `VIOLATION` category; its synchronized toggle control is
visible in RTL, but data stability is not proven by path presence. A manually
checked HMAC socket-to-register connection and two HMAC FIFO mux influences
also gate path presence; the selector is control influence, not positional
data flow. Two HMAC digest/selector-to-ready nonpaths provide paired negative
probes. Six EDN
packed-array lane connections additionally gate exact source and destination bit ranges,
direct evidence, and no conflicting lane row for the same port pair. Two AES
SubBytes-to-ShiftRows share paths gate exact 128-bit correspondence in both
the AES IP and SoC reports. These small references do not measure whole-design
accuracy. Five more AES PRNG reseed lanes in each report pin the generated
160-to-five-by-32 split with exclusive direct bit ranges; they do not validate
all indexed part-selects in the SoC.
Two OTP response-valid inputs in the SoC report gate possible paths through
the variable-index response steering assignment; their `approximate` kind
does not claim a particular partition is selected on every response.
`forbidden_connections[*]` provides bounded negative path probes. Each lists
an exact `source`/`dest` pair that must have no connection row; both endpoints
must appear independently in the report, and duplicate or required-and-
forbidden pairs are rejected. The AES share swap, EDN0/EDN1 misroutes,
OTP FIFO valid-to-ready misroute, and two cross-instance SHA3 control paths
are pinned-RTL examples. Passing these
probes is not a whole-design precision estimate.

In a golden YAML, `known_connections[*]` requires matching `source` and `dest`.
Optional `kind`, `source_bits`, and `dest_bits` tighten the match;
`exclusive: true` also rejects conflicting rows for that port pair.
`sample_connections.py` selects five SHA-256-ranked rows per direct/approximate
and ranged/unranged stratum from the SoC report. The committed
`golden/top_earlgrey_connection_sample.yaml` records source-backed verdicts;
`evaluate.py` rejects a changed connection population, missing labels, or a
contradicted sampled row. Regenerate the selection for inspection with:

```bash
python3 sample_connections.py results/top_earlgrey/conn/connect_report.json
python3 sample_connections.py results/top_earlgrey/conn/connect_report.json \
  --annotations golden/top_earlgrey_connection_sample.yaml
```

The first 20-row audit has 20 confirmed and zero contradicted or unresolved.
The lockstep buffer bit 513 was separately checked by summing the widths of
all 30 lower concatenation operands. It is a deliberately small, correlated sample—not a
whole-design precision or recall estimate. A changed population requires
fresh manual adjudication rather than silently reusing old verdicts.

An independent source-derived recall probe walks Slang's elaborated AST for
seven SoC IP scopes and enumerates single-bit or whole-width, one-dimensional
`logic` sibling-instance port pairs (up to 4,096 bits) sharing one named net with exactly one
output driver and matching widths, or joined by one uniquely driven direct
continuous alias with no other observed writer. The current frame contains
358 expected pairs (355 shared-net and three alias paths); all 358 appear as
`direct` rows. The probe
rebuilds its expectations from pinned RTL on each benchmark run, writes the
full pair manifest to `results/top_earlgrey/source_recall.json`, and fails if
a path is missing, downgraded to `approximate`, or the expected-pair frame hash
changes without re-adjudication. It checks whole-vector path presence, not
bit-lane correspondence, and excludes sliced/converted buses, procedural
glue, interface ports, multi-stage aliases, and unselected SoC scopes. Thus
358/358 is not
whole-design recall. The benchmark requires the `slang` CLI on `PATH` or the
`SVLENS_SLANG` environment variable pointing to its executable.

`reference_crossings[*]` checks exact FF endpoints and top-clock root labels.
An optional `category` pins the classification on the same rooted path; this
does not certify CDC protocol correctness.
`guidance_references[*]` pins an exact FF path, category, required recommendation
fragments, and forbidden fragments. The three SoC probes protect the two SPI
and one power-manager violation messages from unsafe one-size-fits-all advice;
matching text is not a CDC safety proof.
`reset_unresolved_references[*]` pins a sink reset path that must remain
observed without a claimed unique FF driver. The HMAC probe passes through
`rstmgr`'s scan-select reset mux and also pins that mux's exact output, two
inputs, selector, immediately connected reset/scan nets, and the selector's
computed signal dependency, plus the input-0 candidate FF path, domain, and
non-inverted input-relative polarity; it
forbids inventing a direct selector source or an input-1 FF.
It is not a positive RDC
safety verdict. The report separately counts reset paths with mux candidates,
distinct mux outputs, traces hitting the traversal bound, and any unsafe
unique-FF claims on mux-bearing paths with unresolved selectors. The benchmark
fails if the last count is nonzero; a known 0/1 selector may legitimately
trace one branch, but that is not RDC timing sign-off.
It also counts reset paths with branch FF candidates and distinct candidate
FFs; these are topology evidence, not selected runtime mux outputs.
For AES and the full SoC, separate `cdc_sva` runs emit SVA without changing the
primary CDC reports. `sva_references[*]` pins an exact CDC crossing, a primary
or secondary JSON assertion ID, and fragments inside that assertion's property
body. The evaluator requires a fresh JSON/SVA pair, a one-to-one match between
all emitted assert labels and JSON IDs, and identical crossing classifications
between primary and SVA runs. AES pins the unbuffered destination-to-source
data-hold contract; the SoC pins a 2FF transfer, the same crossing's secondary
FIFO Gray assertion, write/read FIFO no-step assertions, a req/ack contract,
and a source-to-destination data-hold contract (6/6 references). All 399 SoC
assert labels must link to JSON. None is a CDC safety verdict.

## Quick Start

```bash
make bench
```

## Manual Steps

```bash
cd bench/opentitan
bash fetch.sh                    # Clone OpenTitan (one-time, ~2GB)
python3 gen_filelist.py          # Generate .f filelists from .core files
bash run.sh                      # Run svlens on all targets
python3 evaluate.py              # Summarize counts and check labeled path presence
```

Results appear in `bench/opentitan/results/bench_report.md`; raw JSON reports
and logs remain beside it. The benchmark also runs weekly and on demand in
`.github/workflows/opentitan-benchmark.yml`, which uploads the reports as an
artifact and elaborates the emitted AES and SoC SVA alongside the pinned RTL.

## Targets

| Level | IP | Top Module | Purpose |
|-------|----|-----------|---------|
| L1 | aes | aes | Multi-clock IP |
| L2 | hmac | hmac | Simple baseline |
| L3 | uart | uart | External interface |
| L4 | top_earlgrey | top_earlgrey | Full SoC scale |

## Requirements

- svlens built (`make build`)
- `slang` CLI on `PATH` (or `SVLENS_SLANG` set to its executable)
- Python 3.10+ with PyYAML (`pip install pyyaml`)
- Git
- ~2GB disk for OpenTitan clone

The default `setup-deps.sh` install contains the slang library but may omit
the CLI. For a fresh benchmark prefix, run `./scripts/setup-deps.sh --prefix
<dedicated-prefix> --with-tools` from the repository root and set
`SVLENS_SLANG=<dedicated-prefix>/bin/slang` when running `make bench`. A cached
library-only prefix is rejected instead of silently skipping AST checks.

The runner works on macOS and Linux. It records elapsed wall time; peak RSS is
left unavailable because the old GNU `time -v` path was not portable.
Filelist generation selects FuseSoC's default RTL filesets, enables
`SYNTHESIS`, and uses a single compilation unit. Since this source-only run
does not invoke FuseSoC primgen, it creates benchmark-local `prim_*` modules
from OpenTitan's `prim_generic_*` implementations under `filelists/generated/`.
These are generic RTL aliases, not a production technology-library build.
Missing selected dependencies or sources fail filelist generation. A target
without a fresh JSON report is marked `no_report` and excluded from measured
counts; `evaluate.py` fails if any target is incomplete, a required labeled
connection or CDC crossing is absent, a CDC reference has mismatched root
labels, or an SVA reference or JSON label is missing. The SoC sample audit
additionally fails on stale or contradicted labels. Labeled-path presence is
not a precision/recall estimate.

For `top_earlgrey`, the runner also creates a separate `cdc_periods` report
using [`constraints/top_earlgrey_periods.sdc`](constraints/top_earlgrey_periods.sdc).
It projects four ASIC SDC base-clock periods onto the corresponding submodule
ports verified in `chip_earlgrey_asic.sv`; it is not a sign-off SDC. The
evaluator requires fresh output, an unchanged SDC hash, all four base periods
in CDC domain JSON, at least one populated timing basis, unchanged
per-crossing structural classifications, and the power-manager period/root
reference. The primary no-SDC CDC report and its
root-label references remain separate. Period presence does not prove
capture stability, path delay, phase, or reset safety.
