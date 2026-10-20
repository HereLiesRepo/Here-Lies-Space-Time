# hhl-angle: two streams the stock grader accepts that are wrong

Grader repository `andigu/uncompute` at commit 64b5a13, task `hhl-angle`.

Both reproducers are deliberate, minimal bugs planted in the **authors' own
released circuit**, `tasks/hhl-angle/circuits/discovered` (1,348 Toffolis x
115 qubits, stream sha256 `99e0420de4dad782c0e8f1b5e8429ebd1c49035fc603e417c02a54374ad7513c`).
Nothing else went into them. Each one is graded **OK** by the unchanged
grader with 0 failed shots, is wrong by about 2^30 or 2^32 output units on
concrete `(X, k)` inputs, and is rejected by the carry-boundary patch in
`verifier/uncompute_hhl_k_coverage.patch`. `verifier/GAP_REPORT.md` explains the gap.

| reproducer | how it was made from the authors' circuit | stock grader | T x Q | failing lanes (ucverify --strict) | patched grader |
|---|---|---|---|---:|---|
| `coverage-gap-carry30` | product row 0 adds into `k[4:30]` instead of `k[4:33]` (generator edit) | OK, 0 of 8,747 shots failed | 1,345 x 115 = 154,675 | 1,221 | FAIL, 469 wrong-output shots |
| `coverage-gap-blip` | one CX removed from the stream: the carry of product row 0 into `k` bit 32 | OK, 0 of 8,748 shots failed | 1,348 x 115 = 155,020 | 609 | FAIL, 320 wrong-output shots |
| control: `discovered` unchanged | none | OK, 0 of 8,741 shots failed | 1,348 x 115 = 155,020 | 0 (PASS) | OK, 0 of 6,103,589 shots failed |

## Contents

```
reproducers/
  README.md                          this file
  hhl-angle/<name>/
      stream.ops.xz      the op stream (xz of a raw OPSTRM01 file)
      grade.json         stock grader output, tools/eval.py hhl-angle --ops ... --json
      patched_grade.json patched grader output for the same stream
      exact_check.log    ucverify count + per-pattern summary of ucverify check --strict
      failing_lanes.txt  every failing lane, with the long-double recheck
      SHA256SUMS
  controls/authors-discovered/       grade.json and patched_grade.json for the unchanged circuit
  controls/authors-haner-piecewise/  patched_grade.json for the authors' other hhl-angle circuit
  tools/carry30_circuit.diff         the generator edit for coverage-gap-carry30
  tools/opsdrop.c                    removes one op from a loop-free stream (used for coverage-gap-blip)
verifier/
  GAP_REPORT.md                      the coverage gap, with exact numbers for these reproducers
  uncompute_hhl_k_coverage.patch     proposed grader change (unified diff against 64b5a13)
  lanes_carry30.txt, lanes_blip.txt  every failing (X, k) lane, one per line (= failing_lanes.txt)
```

## How each reproducer was made

The authors' generator rebuilds their stream byte for byte:

```sh
cd tasks/hhl-angle/circuits/discovered
UNCOMPUTE_OPS_PATH=discovered.ops uv run python circuit.py
sha256sum discovered.ops   # 99e0420de4dad782c0e8f1b5e8429ebd1c49035fc603e417c02a54374ad7513c
```

### coverage-gap-carry30: a truncated carry chain

`circuit.py` subtracts the product `P(dn, v)` from `k` as seven signed Booth
digit rows. Row `t` is added into the slice `KQ[B4[t]:]`, i.e. `k` bits
`B4[t] .. 32`, with the row's sign extension and carry rippling up to bit 32.
The edit gives row 0 (the smallest row, `B4[0] = 4`) the slice `KQ[4:30]`
instead. That row then works modulo 2^30: its carry or borrow never reaches
`k` bits 30, 31 and 32. The edit is two lines (`tools/carry30_circuit.diff`;
the second line only updates the cost pin in `meta.json` so that the
generator's own `assert_metrics` accepts the 3 fewer tail Toffolis):

```diff
--- a/tasks/hhl-angle/circuits/discovered/circuit.py
+++ b/tasks/hhl-angle/circuits/discovered/circuit.py
@@ -441,7 +441,7 @@
         with b.phase(f"product/t{t}"):
             cxs, s1w, m1w, signw = prod_row_spec(t)
             m, n = VW - SIG[t] + 1, 33 - B4[t]
-            brow(cxs, s1w, m1w, signw, prod_pairs(t), KQ[B4[t] :], row_gl(m, n))
+            brow(cxs, s1w, m1w, signw, prod_pairs(t), (KQ[B4[t] : 30] if t == 0 else KQ[B4[t] :]), row_gl(m, n))
--- a/tasks/hhl-angle/circuits/discovered/meta.json
+++ b/tasks/hhl-angle/circuits/discovered/meta.json
-  "toffoli": 1348,
+  "toffoli": 1345,
```

```sh
patch -p1 < <release>/reproducers/tools/carry30_circuit.diff   # run from the grader repository root
cd tasks/hhl-angle/circuits/discovered
UNCOMPUTE_OPS_PATH=carry30.ops uv run python circuit.py
sha256sum carry30.ops   # 1d82b1b0a776ca7886625d65f9c91d5dc48cf1b134ec9363b17b2c837f7d3997
```

Row 0's addend is at most about 2^12 units, so the row crosses a multiple of
2^30 only when `k` is within about 2^12 of one at that point. Every failing
lane has its ideal result `k*` within 7,324,648 units (< 2^23) of a multiple
of 2^30, and is off by ±2^30 units (1,073,741,422 to 1,073,742,186). The sign
is correct on every lane; only the output is wrong.

### coverage-gap-blip: one op dropped

At the top of product row 0's carry ripple, the authors' stream writes the
carry into `k` bit 32 with one CX:

```
op 11917   CX  control = qubit 114 (row 0's last carry AND)   target = qubit 53 (k bit 32)
```

It is CX number 9,170 of the stream, counting CX ops from 0 in stream order
(phase `product/t0`). The reproducer is the authors' stream with exactly
that op record removed; the FLAT segment's op count and the END op count each
drop by one, and every other byte is unchanged:

```sh
cc -std=c11 -O2 -o opsdrop <release>/reproducers/tools/opsdrop.c   # then, where discovered.ops was written:
./opsdrop CX 9170 < discovered.ops > blip.ops
#   opsdrop: dropped CX #9170 (op 11917, byte 118682, operands 114 53, flags 0)
sha256sum blip.ops   # 4b6d1d9ba17b8779c8301fffe2dd870f7b4af356a902efaa792aa49ecefc4e53
```

The carry AND on qubit 114 is still computed and erased correctly, so
nothing is left dirty and no phase changes. The only effect is that bit 32
misses row 0's carry: the result is off by ±2^32 units (4,294,966,894 to
4,294,967,294) whenever row 0 crosses a multiple of 2^32. Every failing `k*`
lies within 3,372,074 units (< 2^22) of a multiple of 2^32. `ucverify`'s own
mutation option reproduces it without writing a file:
`ucverify check hhl-angle discovered.ops --strict --drop CX:9170` lists the
identical 609 lanes.

## Results

### Stock grader (unchanged, 64b5a13)

`python tools/eval.py hhl-angle --ops FILE --json`. A re-run on 2026-10-20
(pure-Python reference executor, `--jobs 4`) reproduced every `grade.json` here exactly apart
from `timing`. The `patched_grade.json` files come from that re-run.

| stream | verdict | toffoli | qubits | tq_cost | shots | wrong output | wrong sign | h_ops |
|---|---|---:|---:|---:|---:|---:|---:|---|
| coverage-gap-carry30 | OK | 1345 | 115 | 154,675 | 8,747 | 0 | 0 | `1d82b1b0…3997` |
| coverage-gap-blip | OK | 1348 | 115 | 155,020 | 8,748 | 0 | 0 | `4b6d1d9b…4e53` |
| discovered (control) | OK | 1348 | 115 | 155,020 | 8,741 | 0 | 0 | `99e0420d…513c` |

### ucverify, exhaustive in X (`--strict`, 45 k patterns)

| pattern | carry30 | blip | control |
|---|---:|---:|---:|
| zero, ones | 0 | 0 | 0 |
| xormix | 4 | 2 | 0 |
| splitmix:1 / splitmix:5 | 1 / 0 | 0 / 0 | 0 |
| random:1 / 2 / 3 / 4 | 1 / 1 / 1 / 1 | 0 / 0 / 0 / 1 | 0 |
| kneg:0 | 152 | 152 | 0 |
| kneg:2^30 - 1, 2^30 | 151, 152 | 0, 0 | 0 |
| kneg:2^31 - 1, 2^31 | 151, 152 | 0, 0 | 0 |
| kneg:2^32 - 1, 2^32 | 151, 152 | 151, 152 | 0 |
| kneg:2^33 - 1 | 151 | 151 | 0 |
| **total failing lanes** | **1,221** (161 distinct X) | **609** (155 distinct X) | **0, PASS** |

All other patterns have 0 failing lanes. Every failing lane is `wrong_output`;
none has a wrong or outcome-dependent sign and none leaves a dirty qubit.
`ucverify ref` rechecks every listed lane with long-double `asinl`: the f64
and long-double `f(X)` agree on all 1,830 lanes and none is within the
429-unit band.

Examples:

| reproducer | pattern | X | k | k_out | ideal k* | error (units) |
|---|---|---:|---:|---:|---:|---:|
| carry30 | xormix | 430,363 | 8,259,225,297 | 7,512,820,785 | 8,586,562,518 | -1,073,741,733 |
| carry30 | xormix | 1,446,615 | 2,049,862,727 | 3,220,900,487 | 2,147,158,501 | +1,073,741,986 |
| carry30 | random:3 | 111,203 | 4,076,723,685 | 4,287,642,485 | 5,361,384,472 | -1,073,741,987 |
| carry30 | kneg:0 | 66,560 | 6,379,147,389 | 1,073,741,645 | 0 | +1,073,741,645 |
| blip | xormix | 430,363 | 8,259,225,297 | 4,291,595,313 | 8,586,562,518 | -4,294,967,205 |
| blip | xormix | 1,104,508 | 4,164,796,877 | 8,587,203,581 | 4,292,236,553 | +4,294,967,028 |
| blip | kneg:0 | 66,560 | 6,379,147,389 | 4,294,967,117 | 0 | +4,294,967,117 |

### Patched grader (64b5a13 + `uncompute_hhl_k_coverage.patch`)

| stream | verdict | shots | wrong output | wrong sign | first failing shot |
|---|---|---:|---:|---:|---:|
| coverage-gap-carry30 | **FAIL** (verdict/classical) | 6,103,595 | 469 | 0 | 10,048 |
| coverage-gap-blip | **FAIL** (verdict/classical) | 6,103,596 | 320 | 0 | 10,048 |
| discovered (control) | **OK** | 6,103,589 | 0 | 0 | none |
| haner-piecewise (the authors' other circuit) | **OK** | 6,103,576 | 0 | 0 | none |

Shot 10,048 is in the appended sweep (shots 9,024 and up), at boundary
`B = 0`.

## Reproduce

    git clone https://github.com/HereLiesRepo/Here-Lies-Space-Time release
    git clone https://github.com/andigu/uncompute && cd uncompute && git checkout 64b5a13 && uv sync
    xz -dc ../release/reproducers/hhl-angle/coverage-gap-blip/stream.ops.xz > blip.ops
    sha256sum blip.ops                                          # = grade.json h_ops
    uv run python tools/eval.py hhl-angle --ops blip.ops --json # verdict OK
    make -C ../release/verifier                                 # builds ucverify
    ../release/verifier/ucverify check hhl-angle blip.ops --strict --list 20   # verdict FAIL
