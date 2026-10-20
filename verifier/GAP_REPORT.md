# hhl-angle: the random-k test misses carry-boundary errors

Grader repository `andigu/uncompute` at commit 64b5a13, task `hhl-angle`.

## Summary

The hhl-angle contract requires the map
`|X>|k> -> |X>|k + f(X) mod 2^33>` for every in-domain `X` and for an
arbitrary accumulator `k` (README section 1: "an arbitrary accumulator
`k`"; section 3.1: "Its initial value is arbitrary").

The grader tests about 8,744 shots, each with an independent uniform
`k`. Some wrong adders are wrong only when the ideal result
`k* = k + f(X) mod 2^33` lies next to a carry boundary, that is, when a long
run of its bits is all ones or all zeros. A uniform `k` produces such a run
with probability about 2^-r for a run of length r. So a stream whose adder
mishandles long carries can be accepted.

We give two reproducers. Both are deliberate one-place bugs in the authors'
own released circuit, `tasks/hhl-angle/circuits/discovered` (README.md says
exactly how each was made). The unchanged grader accepts both. Both are
wrong for some `(X, k)` by at least 2^30 output units, where the band is 429.

| reproducer | official result (unchanged grader) | ucverify, exhaustive `X` (strict suite) |
|---|---|---|
| `coverage-gap-carry30` | OK, 1,345 T x 115 Q, 8,747 shots, 0 failed | FAIL on 1,221 (X, k) lanes |
| `coverage-gap-blip` | OK, 1,348 T x 115 Q, 8,748 shots, 0 failed | FAIL on 609 (X, k) lanes |

The attached patch (`uncompute_hhl_k_coverage.patch`) adds a deterministic
carry-boundary sweep. With the patch, the grader rejects both reproducers.
It still accepts the unmodified `discovered` circuit.

## 1. Reproducers

| file | made from `discovered` by | sha256 (raw stream) | ops | T | Q |
|---|---|---|---:|---:|---:|
| `coverage-gap-carry30` | product row 0 added into `k[4:30]` instead of `k[4:33]` | `1d82b1b0a776ca7886625d65f9c91d5dc48cf1b134ec9363b17b2c837f7d3997` | 58,956 | 1,345 | 115 |
| `coverage-gap-blip` | CX #9,170 removed (row 0's carry into `k` bit 32) | `4b6d1d9ba17b8779c8301fffe2dd870f7b4af356a902efaa792aa49ecefc4e53` | 58,976 | 1,348 | 115 |
| `discovered` (control, unchanged) | none | `99e0420de4dad782c0e8f1b5e8429ebd1c49035fc603e417c02a54374ad7513c` | 58,977 | 1,348 | 115 |

All are ordinary OPSTRM01 streams (REGS + FLAT + END) with two Qubit
registers, `X` (21) and `k` (33).

## 2. The unchanged grader accepts both

We ran the grader at 64b5a13, unmodified, with
`python tools/eval.py hhl-angle --ops FILE --jobs 2 --json`. Runs went one at
a time under a shared lock. The checkout had no compiled kernel, so the
evaluator used its pure-Python reference executor, which applies the same
draws and checks.

| stream | verdict | toffoli | qubits | tq_cost | shots | wrong output | wrong sign | dirty | h_ops |
|---|---|---:|---:|---:|---:|---:|---:|---:|---|
| coverage-gap-carry30 | OK | 1345 | 115 | 154,675 | 8,747 | 0 | 0 | 0 | `1d82b1b0…3997` |
| coverage-gap-blip | OK | 1348 | 115 | 155,020 | 8,748 | 0 | 0 | 0 | `4b6d1d9b…4e53` |
| discovered (control) | OK | 1348 | 115 | 155,020 | 8,741 | 0 | 0 | 0 | `99e0420d…513c` |

## 3. ucverify gives the failing lanes

`ucverify check hhl-angle FILE --strict` sweeps all 2,031,616 in-domain
`X` for each of 45 accumulator patterns:

- the structured patterns `zero`, `ones`, `xormix`, `splitmix:1` and `splitmix:5`;
- four independent uniform-`k` seeds;
- carry-boundary patterns `k = D - f(X)` for `D = 0`, `2^j - 1` and `2^j`.

Every lane that fails in any pattern is listed in `lanes_carry30.txt` and
`lanes_blip.txt`, one line per lane, with these columns:

```
kpattern X k k_out k_ideal f64 ext delta wrong_output outdep
```

Failing lanes per pattern (patterns not listed have 0):

| pattern | coverage-gap-carry30 | coverage-gap-blip | discovered |
|---|---:|---:|---:|
| zero, ones | 0 | 0 | 0 |
| xormix | 4 | 2 | 0 |
| splitmix:1 | 1 | 0 | 0 |
| splitmix:5 | 0 | 0 | 0 |
| random:1 / 2 / 3 / 4 | 1 / 1 / 1 / 1 | 0 / 0 / 0 / 1 | 0 |
| kneg:0 (k* = 0) | 152 | 152 | 0 |
| kneg:2^30 - 1, 2^30 | 151, 152 | 0, 0 | 0 |
| kneg:2^31 - 1, 2^31 | 151, 152 | 0, 0 | 0 |
| kneg:2^32 - 1, 2^32 | 151, 152 | 151, 152 | 0 |
| kneg:2^33 - 1 | 151 | 151 | 0 |
| **total listed lanes** | **1,221** | **609** | **0 (PASS)** |

Examples:

| reproducer | X | k | k_out | ideal k* | error (units) |
|---|---:|---:|---:|---:|---:|
| carry30 | 430,363 | 8,259,225,297 | 7,512,820,785 | 8,586,562,518 | -1,073,741,733 |
| carry30 | 1,446,615 | 2,049,862,727 | 3,220,900,487 | 2,147,158,501 | +1,073,741,986 |
| carry30 | 66,560 | 6,379,147,389 | 1,073,741,645 | 0 | +1,073,741,645 (2^30 - 179) |
| blip | 430,363 | 8,259,225,297 | 4,291,595,313 | 8,586,562,518 | -4,294,967,205 |
| blip | 1,104,508 | 4,164,796,877 | 8,587,203,581 | 4,292,236,553 | +4,294,967,028 |
| blip | 66,560 | 6,379,147,389 | 4,294,967,117 | 0 | +4,294,967,117 (2^32 - 179) |

Both bugs sit in the smallest product row, whose addend is below about 2^12
units, so they fire only when that row crosses a boundary:

- **coverage-gap-carry30** makes row 0 work modulo 2^30. The error is
  ±2^30 units (1,073,741,422 to 1,073,742,186). Every failing ideal result
  `k*` lies within 7,324,648 units (< 2^23) of a multiple of 2^30. It
  fails for 161 distinct `X` across all patterns.
- **coverage-gap-blip** drops row 0's carry into bit 32. The error is
  ±2^32 units (4,294,966,894 to 4,294,967,294). Every failing `k*` lies
  within 3,372,074 units (< 2^22) of a multiple of 2^32. It fails for 155
  distinct `X` (66,560, 66,816, 67,072, 100,352, …), about one `X` in
  13,000.

Neither changes a sign: on every failing lane the sign is correct and
independent of the measurement outcomes, and no qubit is left dirty. The
grader's output check alone must catch them.

## 4. Independent check of each failing lane

`ucverify ref hhl-angle` recomputes each listed lane from the README
formula, `f(X) = round(2^32 asin(2^15 / X))` with ties rounded away from
zero. It uses long-double `asinl`, a different code path from the grader's
f64 `math.asin`. It then takes the centred residue of
`k_out - (k + f(X)) mod 2^33`.

The results, over all 1,830 listed lanes:

- The f64 and long-double values of `f(X)` agree on every lane.
- No listed lane is within the 429-unit band.
- The smallest error is 1,073,741,422 units (about 0.25 rad).

Rounding and precision questions therefore play no part. These lanes violate
the contract by a wide margin.

## 5. What the contract requires, and why random k misses it

The contract quantifies over every in-domain `X` and every 33-bit `k`. Its
README (section 6) says the random accumulator "places tests across the
whole residue space, including the wraparound". That is true of the
distribution of `k*`. But an adder's failure set can be a thin set of
carry-boundary residues, and a uniform sample rarely lands in it.

We measured the per-shot failure probability under the grader's own
distribution: uniform in-domain `X` and uniform `k`, using
`random:101` … `random:150`, i.e. 50 × 2,031,616 = 101,580,800 lanes.

| reproducer | failing lanes / 101,580,800 | p per shot (95% interval) | expected failures in 8,744 shots | P(grader catches it) |
|---|---:|---|---:|---:|
| coverage-gap-carry30 | 43 | 4.23e-7 (3.06e-7 – 5.70e-7) | 0.0037 | 0.37 % |
| coverage-gap-blip | 12 | 1.18e-7 (0.61e-7 – 2.06e-7) | 0.0010 | 0.10 % |

The intervals are exact Poisson intervals. The catch probability is
`1 - (1 - p)^8744`.

The grader's test inputs are a fixed function of the stream hash, so a
stream is either always accepted or always rejected. A one-op change to the
stream gives fresh draws, but each draw still has only the probabilities
above. Raising the shot count does not close the gap at a useful price.
Catching coverage-gap-blip with 99 % probability needs about 3.9 × 10^7
uniform shots, and coverage-gap-carry30 about 1.1 × 10^7. The
carry-boundary sweep in the patch is deterministic, and it catches both
reproducers with hundreds of failing shots.

A second, independent effect concerns signs. The evaluator draws one
random outcome per lane (`strict=False`), so a lane whose sign depends on a
measurement outcome shows as wrong only half the time. Neither reproducer
here has such a lane; ucverify's `--drop CZ:100` negative control
(`make check`) shows the effect on the same circuit.

## 6. The proposed patch

`uncompute_hhl_k_coverage.patch` is a unified diff against 64b5a13. It
changes `tasks/hhl-angle/grader.py` and the testing section of
`tasks/hhl-angle/README.md`:

- Shots 0–9,023 are unchanged: the same draws and the same skips, from the
  same per-shot XOF.
- 3 × 2,031,616 = 6,094,848 shots are appended. For each boundary
  `B ∈ {0, 2^31, 2^32}`, and for every in-domain `X` in order, the patch
  sets `k = (B + d - f(X)) mod 2^33`. Here `d ∈ [-128, 127]` is one byte of
  that shot's own XOF, so the seeding scheme is kept. The ideal result is
  then `B + d`, and it sits on both sides of the boundary.
- `num_tests` becomes 9,024 + 6,094,848. The check, the band and the
  failure policy (STRICT) are unchanged.

We applied it to a scratch copy of the repository (the original checkout
was not modified) and ran the same command as in section 2:

| stream | expected | patched grader | T x Q | shots | wrong output | wrong sign | time |
|---|---|---|---|---:|---:|---:|---:|
| coverage-gap-carry30 | reject | **FAIL** (verdict/classical) | 1345 x 115 | 6,103,595 | 469 | 0 | 92 s |
| coverage-gap-blip | reject | **FAIL** (verdict/classical) | 1348 x 115 | 6,103,596 | 320 | 0 | 72 s |
| discovered, unchanged (99e0420d…) | accept | **OK** | 1348 x 115 | 6,103,589 | 0 | 0 | 84 s |

Both rejections report shot 10,048 first, which is in the sweep at
`B = 0`. Times are wall-clock with `--jobs 2` (C kernel executor) on a
4-core host that was shared with other jobs.

Runtime cost: the unchanged grader runs 2 batches of 8,192 lanes. The
patched grader runs 746 batches. The cost scales linearly in the number of
boundaries. A single boundary at `2^32` would already catch both
reproducers (152 and 152 failing lanes at `kneg:2^32` in the exhaustive
check) at about one third of the cost.

## 7. Scope and limits

- This report is about hhl-angle. coulomb-potential, gaussian and
  lattice-gauge-plaquette also add into a modular accumulator with a uniform
  random `k`, so the same argument applies to them in principle. We did not
  build reproducers for those tasks. ucverify's `--strict` suite runs the
  same carry-boundary patterns on coulomb-potential and gaussian.
- The sweep tests every `X` at three boundary families, not every
  `(X, k)` pair. An adder can still fail on residues outside these
  families. The patch closes the specific gap shown here; it does not prove
  that the adder is correct.
- ucverify was written from the format specification in this repository
  and shares no code with it. Its counts equal the published numbers for
  all 17 stored paper circuits (`make check`).
