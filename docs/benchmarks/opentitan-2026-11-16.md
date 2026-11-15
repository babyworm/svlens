# Four-state constant-arm and AXI reset-phase checks

Sources remain pinned to OpenTitan `ed044fc9760bdf9fc075d0015ba1db07fa075355`,
PULP AXI `70b8e54fd460e3308e58be596ceb3566a6e3576e`, and common_cells
`e73baaec2ca665cd80c3c384e9258e35242b829c`. The local candidate binary is
svlens 0.3.7. These are bounded structural and behavioral probes.

## SPI interpretation and the actual missing-path defect

The earlier SPI CSID false-positive diagnosis applied only to binary values.
For `invalid = q >= 1; csid = invalid ? 0 : q[0]`, binary q=0 selects a zero
low bit and every other binary q selects the zero arm. But q=`32'h0000000x`
or `32'h0000000z` makes the predicate unknown and the merged output X.
An unknown high nibble with a known zero low bit still yields zero; with a
known one low bit it yields X. Slang constant evaluation and an independent
Icarus four-state execution reproduce these distinctions. The regression also
checks limit=2, where binary q=1 really carries a one.

The extractor must retain these paths as `approximate` structural may-flow.
They do not certify binary value influence. The previous unconditional
classification of 16 correlated SPI rows as false positives is withdrawn.
Two pinned SPI paths now explicitly gate this four-state interpretation.

The regression exposed a separate real defect: promoting `1'b0` to a
four-state ternary arm caused the extractor to reject the whole expression,
including the nonconstant arm. Constants now contribute no source dependency
before conversion rejection, preserving the other arm and selector as
approximate evidence. Nonconstant state-changing conversions retain their
existing conservative treatment. An AES PRNG request-to-ack path independently
gates the recovered shape against `aes_prng_clearing.sv:49`.

## Observed reports

| Target | Connections | Direct | Approximate | Bit-flow gaps |
|--------|------------:|-------:|------------:|--------------:|
| AES | 2,303 | 1,593 | 710 | 696 |
| HMAC | 174 | 107 | 67 | 192 |
| UART | 341 | 276 | 65 | 167 |
| SoC | 97,550 | 64,879 | 32,671 | 25,545 |

The SoC adds 333 approximate rows and removes none. Its 20 selected sample IDs
are unchanged and confirmed. Population SHA-256 is
`337c5c6034e8d2bea651f3cfd8f7446677ccab21e213d5c2588a3c0affd99a55`.
Selected positive gates pass 39/39 and absent-path gates 7/7. Source-derived
direct, pass-through, and ternary-control frames remain 358/358, 16/16, and 6/6.
These counts do not establish whole-design precision or recall.

The PULP report has 1,072 rows and 130 gaps. All 108 previously pinned
relations remain present; newly exposed rows are not all adjudicated.

## Functional reset-phase coverage

The two outstanding-traffic reset scenarios each run at offsets 1, 4, 6, and
9 ns after a falling edge of the 10 ns clock. Requests already accepted before
the delayed reset are dropped at the falling edge, preventing accidental
duplicate handshakes. Pending responses remain blocked until reset.
The positive run completes 14 route cases, 28 writes, 28 reads, and eight reset
recoveries. Corrupted-response and both omitted-reset controls still fail at
their expected assertions. Testbench SHA-256 is
`9b30740aedc25986b6829192917b637d41052dfadcc35b72ad5a824dda5dc284`.

The fixture enforces a shared reset and endpoint discard contract. Independent
endpoint resets need additional isolation/flush logic: AXI-Lite has no reset
epoch to distinguish a stale held-valid request from a new one. The four
offsets are functional simulation, not proof of physical recovery/removal
timing, metastability safety, or all arbitration schedules.
