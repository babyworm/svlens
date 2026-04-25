# Fuzz harnesses

libFuzzer-based fuzzers for the parsers that consume user-provided text:

| Harness | Target |
|---------|--------|
| `fuzz_filelist` | `sv_cdccheck::FilelistParser::parseString` |
| `fuzz_sdc` | `sv_cdccheck::SdcParser::parse` |
| `fuzz_waiver` | `sv_cdccheck::WaiverManager::loadString` |
| `fuzz_clock_yaml` | `sv_cdccheck::ClockYamlParser::loadString` |

## Build

Requires Clang with libFuzzer.

```bash
cmake -B build-fuzz \
  -DCMAKE_CXX_COMPILER=clang++ \
  -DSVLENS_ENABLE_FUZZ=ON \
  -DSVLENS_FETCH_DEPS=ON
cmake --build build-fuzz -j"$(nproc 2>/dev/null || sysctl -n hw.ncpu)"
```

## Run

Each binary takes an optional corpus directory. Persistent corpora live under
`fuzz/corpus/<name>/`; create one once and reuse across runs:

```bash
mkdir -p fuzz/corpus/filelist fuzz/corpus/sdc fuzz/corpus/waiver fuzz/corpus/clock_yaml

./build-fuzz/fuzz/fuzz_filelist   fuzz/corpus/filelist   -max_total_time=120
./build-fuzz/fuzz/fuzz_sdc        fuzz/corpus/sdc        -max_total_time=120
./build-fuzz/fuzz/fuzz_waiver     fuzz/corpus/waiver     -max_total_time=120
./build-fuzz/fuzz/fuzz_clock_yaml fuzz/corpus/clock_yaml -max_total_time=120
```

Common libFuzzer flags:

- `-runs=N` -- stop after N executions
- `-max_total_time=SEC` -- stop after wall-clock time
- `-jobs=N -workers=N` -- run N parallel processes
- `-detect_leaks=0` -- disable leak detection if it gets noisy

Crashes are written as `crash-*` next to the harness; reproduce with:

```bash
./build-fuzz/fuzz/fuzz_sdc crash-abc123
```

## Sanitizers

The harnesses are compiled with `-fsanitize=fuzzer,address,undefined`. To
add MSAN, rebuild with `-fsanitize=fuzzer,memory` (note: MSAN requires all
linked dependencies to be MSAN-instrumented, including slang).
