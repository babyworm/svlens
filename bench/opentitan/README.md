# OpenTitan Benchmark for svlens

Measures svlens accuracy and performance against real SoC RTL.

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
python3 evaluate.py              # Compute recall/precision
```

Results appear in `bench/opentitan/results/bench_report.md`.

## Targets

| Level | IP | Top Module | Purpose |
|-------|----|-----------|---------|
| L1 | aes | aes | Multi-clock IP |
| L2 | hmac | hmac | Simple baseline |
| L3 | uart | uart | External interface |
| L4 | top_earlgrey | top_earlgrey | Full SoC scale |

## Requirements

- svlens built (`make build`)
- Python 3.8+ with PyYAML (`pip install pyyaml`)
- Git
- `/usr/bin/time` (for memory measurement)
- ~2GB disk for OpenTitan clone
