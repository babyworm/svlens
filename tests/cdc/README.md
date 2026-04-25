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
| `Ac_cdc04` | wide-bus crossing without gray code or handshake | `15_bus_cdc_no_gray` (1 CAUT) | `32_neg_ac_cdc04_single_bit` (1 INFO) |
| `Ac_cdc05` | flop clock driven by combinational expression (raw mux without glitch-free primitive or SDC declaration) | `21_clock_mux` (1 VIOL) | `34_neg_ac_cdc05_safe_mux_cell` (registered safe cell via `--glitch-free-mux-cell`, 0/0/0), `35_neg_ac_cdc05_sdc_clock_mux` (SDC `create_generated_clock`, 0/0/0) |

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
| `25_nested_sync_clock_inherit` | 3-level clock-domain inheritance through wrappers |

## Wide-bus crossings (Ac_cdc04)

These fixtures use multi-bit registers crossing through plain
TwoFF/ThreeFF chains. Even though each bit individually has a proper
synchronizer, bits resolve metastability at different cycles, so the
receiver can momentarily see intermediate values. svlens flags this
as Ac_cdc04 ("wide-bus crossing without gray code or handshake").
The legitimate fix is gray coding the bus or gating with a single-bit
handshake -- see fixture 08 (gray_fifo_ptr) and 07 (handshake_req_ack).

| Fixture | Pattern |
|---------|---------|
| `15_bus_cdc_no_gray` | 4-bit bus through per-bit 2-FF chain (1 CAUT Ac_cdc04) |
| `23_packed_array_indexed` | per-bit 2-FF inside generate-for, no gray code (4 CAUT Ac_cdc04) |
| `24_genfor_module_sync` | per-bit sync MODULE INSTANCE inside generate-for (4 CAUT Ac_cdc04) |
| `26_genfor_in_wrapper_clock_inherit` | clock-inheritance + per-bit gen_sync (4 CAUT Ac_cdc04) |

## Codified detection limitations

These fixtures pin down the current behaviour for cases svlens does not
yet handle precisely. Future enhancements should update both the
fixture comment and the golden value.

| Fixture | Current limitation |
|---------|--------------------|
| `33_always_comb_sync_chain` | downstream `findNextFF` does not yet trace across two adjacent flop_w submodule INSTANCES, so the OpenTitan-style 2-stage chain (two separate prim_flop instances) is reported VIOLATION rather than INFO. The crossing IS detected end-to-end. |

### Shift-register-style synchronizer recognition (resolved)

`pulp-platform/common_cells/src/sync.sv` implements the 2-FF (or N-FF)
synchronizer as a single multi-bit register with an internal shift:

```systemverilog
logic [STAGES-1:0] reg_q;
always_ff @(posedge clk_i, negedge rst_ni)
    if (!rst_ni) reg_q <= '0;
    else         reg_q <= {reg_q[STAGES-2:0], serial_i};
```

svlens models `reg_q` as a single multi-bit FF node. Commit `8d2ec38`
added a fallback in `sync_verifier::detectSyncPattern` that recognises
the self-shift fanin signature as a TwoFF synchronizer, so crossings
through this chain are no longer reported as VIOLATIONs.

`cdc_fifo_gray` (focused filelist) post-fix: 0 VIOLATIONS / 8 CAUTIONS,
where the cautions are Ac_cdc03 reconvergence (multiple bits across
the same domain pair, identical handling to fixture 23). The
underlying data-path tracing remains end-to-end correct.

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
