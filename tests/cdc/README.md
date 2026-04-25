# CDC golden fixture index

This directory holds the regression suite for svlens' CDC analysis. Each
fixture is one self-contained SystemVerilog design with a matching
`golden/<name>.json` recording the expected `(violations, infos,
crossings)` triple, optionally accompanied by a `<name>.sdc` file when
the test exercises SDC-driven clock declarations.

The runner is `tests/test_cdc_golden.sh` (invoked by ctest as the
`integration_cdc_golden` test). It compares the actual svlens output
against the golden values byte-equivalent and fails on any deviation.

## Pairing principle

Every detection rule that fires on at least one fixture has both:

- **Positive (issue) fixture** — exercises the unsafe pattern. svlens
  must report a violation / caution.
- **Negative (clean) fixture** — exercises the same structural pattern
  but with the issue removed. svlens must NOT raise the same rule.

This lets us catch both **false negatives** (a real issue that goes
unflagged) and **false positives** (a clean design that gets flagged
anyway) on every rule.

## Rule coverage matrix

| Rule | Description | Positive | Dedicated negative |
|------|-------------|----------|--------------------|
| `Ac_cdc01` | missing 2-FF synchronizer | `02_missing_sync` (1 VIOL) | `27_neg_ac_cdc01_proper_sync` (1 INFO) |
| `Ac_cdc01` | single-stage destination FF (insufficient) | `14_single_ff_only` (1 VIOL) | `04_three_ff_sync` (1 INFO) |
| `Ac_cdc02` | combinational logic before sync FF | `05_comb_before_sync` (1 CAUT) | `28_neg_ac_cdc02_clean_path` (1 INFO) |
| `Ac_cdc03` | reconvergence (multiple bits, same domain pair) | `12_multi_crossing_mixed` (2 CAUT + 1 VIOL) | `29_neg_ac_cdc03_distinct_pairs` (Ac_cdc03 silent; fan-out CAUT fires instead) |
| `Ac_cdc06` | reset CDC without 2-FF deassert chain | `19_missing_reset_sync` (1 CAUT) | `30_neg_ac_cdc06_synced_reset` (1 INFO) |
| `Ac_cdc11` | source signal crosses to multiple async domains | `20_fanout_mixed_sync` (1 VIOL + 1 CAUT) | `03_two_ff_sync` (1 INFO, single dest) |

The `27`-`30` fixtures are minimal mirrors of their positive
counterparts -- same structural shape, just with the issue removed --
so a regression that re-introduces a false positive on those patterns
will fail the golden immediately.

## "No false positive" guards

Some fixtures exist purely to ensure svlens does NOT invent issues that
aren't there:

| Fixture | Guards against |
|---------|----------------|
| `01_no_crossing` | reporting any crossing on a single-clock design |
| `10_naming_no_false_clock` | promoting a `*_div2`-named data register to a phantom generated clock |
| `17_reset_synchronizer` | reporting a crossing for a reset-sync chain with an external async port |

## Synchronizer-pattern fixtures

Designs that exercise specific real-world synchronizer idioms. Each
should land as INFO (recognised) when the pattern is legitimate.

| Fixture | Pattern |
|---------|---------|
| `06_submodule_sync` | 2-FF sync inside a sub-module instance |
| `07_handshake_req_ack` | bidirectional 4-phase req/ack |
| `08_gray_fifo_ptr` | gray-coded async FIFO pointer (Cummings 2002) |
| `09_pulse_sync` | toggle + 2-FF + XOR pulse synchronizer |
| `11_sdc_async_groups` | SDC `set_clock_groups -asynchronous` + 2-FF sync |
| `13_three_domain_chain` | A -> B -> C, two hops with 2-FF sync each |
| `18_internal_reset_cdc` | async assert + 2-FF deassert chain |
| `22_two_level_submodule_sync` | 2-level submodule depth + continuous-assign rename |
| `23_packed_array_indexed` | per-bit 2-FF inside a generate-for |
| `24_genfor_module_sync` | per-bit sync MODULE INSTANCE inside generate-for |
| `25_nested_sync_clock_inherit` | 3-level clock-domain inheritance through wrappers |
| `26_genfor_in_wrapper_clock_inherit` | clock domain inheritance through wrapper + generate-for |

## Codified detection limitations

These fixtures pin down the current behaviour for cases svlens does not
yet handle precisely. Future enhancements should update both the
fixture comment and the golden value.

| Fixture | Current limitation |
|---------|--------------------|
| `15_bus_cdc_no_gray` | wide-bus crossing without gray code is collapsed into one logical crossing; per-bit skew not flagged |
| `16_comb_between_domains` | combo-logic-before-sync is reported as Ac_cdc03 reconvergence rather than the more precise Ac_cdc02 rule |
| `21_clock_mux` | `assign clk_mux = sel ? clk_a : clk_b;` is recognised as a third clock domain but the glitch hazard at the mux is not flagged |

## Adding a new fixture

1. Pick the next free number `NN` (currently 27 onwards).
2. Create `tests/cdc/basic/NN_<name>.sv` with the design under test.
3. (Optional) Create `tests/cdc/basic/NN_<name>.sdc` if the test
   needs SDC clock declarations. The runner picks it up automatically.
4. Run svlens against the fixture and confirm the `(violations, infos,
   crossings)` it actually produces.
5. Create `tests/cdc/golden/NN_<name>.json` with the matching
   expectations. Schema: `{"expected_violations": N, "expected_infos":
   N, "expected_crossings": N}`.
6. Register the fixture in `tests/test_cdc_golden.sh`:
   `check_fixture "NN_<name>" "<top_module_name>"`.
7. Update this README's appropriate matrix and run
   `bash tests/test_cdc_golden.sh ./build/svlens` to confirm pass.

## Running locally

```bash
make test                                              # full ctest
bash tests/test_cdc_golden.sh ./build/svlens           # CDC suite only
./build/svlens cdc tests/cdc/basic/01_no_crossing.sv \
    --top single_domain --format md                    # one fixture only
```
