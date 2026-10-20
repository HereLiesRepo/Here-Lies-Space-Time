# ucverify

`ucverify` is a standalone verifier for OPSTRM01 op streams, the format the
`uncompute` evaluator reads (arXiv:2610.22367, grader repository
`andigu/uncompute` at commit 64b5a13). It is one C11 file with no
dependencies beyond the C library and `libm`.

It was written from the format as the evaluator's own reader defines it:
`uncompute/ops.py` (layout, operand rules, billing) and
`uncompute/evalcore/compact_reader.py` (the normative interpreter and its
per-op check order), with `_kernel.c`, `loader.py` and `protocol.py` for the
details. It shares no code with them.

This package contains:

| file | what it is |
|---|---|
| `ucverify.c` | the verifier |
| `Makefile` | `make`, `make check`, `make reproduce` |
| `README.md` | this file |
| `GAP_REPORT.md` | a test-coverage gap in the hhl-angle grader, with reproducers |
| `uncompute_hhl_k_coverage.patch` | a proposed grader change for hhl-angle |

## Build

```sh
make            # cc -std=c11 -O2 -Wall -Wextra -Werror -o ucverify ucverify.c -lm
make check      # needs the grader repository; UNCOMPUTE=/path/to/uncompute
```

`make check` does three things:

1. It runs `ucverify count` on every `tasks/*/circuits/*/circuit.ops.xz` and
   requires its Toffoli count, qubit count and TQ-cost to equal the row of
   `results.jsonl` with the same `h_ops`.
2. If `STORE=dir` is given, it requires every `dir/**/stream.ops.xz` to match
   all seven metrics in the `grade.json` beside it.
3. It runs negative controls on `hhl-angle/circuits/discovered`. The
   unmodified stream must pass. With one Toffoli dropped (`--drop CCX:700`),
   or one phase op dropped (`--drop CZ:100`), ucverify must report a
   failure. Dropping the phase op leaves every output correct and makes the
   sign depend on a measurement outcome on 2,048 of 2,031,616 lanes, so this
   control checks the sign tracking.

Streams are piped through `xz -dc`, so `make check` writes no decompressed
files to disk.

## Usage

```text
ucverify count STREAM
ucverify check TASK STREAM [--k MODE]... [--strict] [--ri A,B,C]... [--lanes N]
               [--seed S] [--sample] [--sweep] [--words W] [--list N] [--drop KIND:N]
ucverify ref TASK < lanes.txt
```

`STREAM` is a raw OPSTRM01 file, or `-` for standard input.

### `count`

`count` parses the stream and prints one JSON line:

```json
{"h_ops": "...", "metrics": {"toffoli": 1348, "clifford": 56419, "qubits": 115,
 "outcomes": 1040, "bits": 0, "ops": 58977, "tq_cost": 155020}}
```

The fields equal the `metrics` object of the evaluator's report:

- `h_ops` is the SHA-256 of the raw stream.
- `toffoli` and `clifford` are per-shot billed counts. Every op bills at
  `ops.gate_class` of its kind, and a bit-conditioned op bills at its
  `_CC_PROMOTION` kind (X to CX, CX to CCX, Z to CZ, CZ to CCZ, NEG to Z).
  CCX and CCZ are Toffolis.
- `qubits` is the largest qubit id in the expansion or in a Qubit register,
  plus 1.
- `outcomes`, `bits` and `ops` are the census sizes and the expanded op
  count. Gated-off ops are not counted.

Loops whose subtree holds no data-gated op are counted analytically, so a
stream with 10^9 expanded ops is counted in seconds.

### `check`

`check` runs the stream on concrete inputs. The simulation is bit-sliced,
with 64 lanes per machine word and `--words` words per pass (512 by
default).

Measurement outcomes are free variables. Outcomes condition only Z, CZ and
NEG, so wire values never depend on them, and the global sign of each lane
is an affine GF(2) function of the outcome bits. ucverify tracks that
function exactly: one constant term per lane, and one coefficient per live
`HMR` draw. When an outcome slot is overwritten or the stream ends, a draw
with a nonzero coefficient on some lane means that lane's sign depends on a
measurement outcome. ucverify reports that as `outcome_dependent_sign`. An
`R` on a nonzero wire is counted the same way, and also as
`reset_of_nonzero`. The evaluator's `Simulator(strict=True)` ledger tracks
the same quantity. The evaluator itself runs with `strict=False`: it draws
one random outcome per lane, so it catches such a lane with probability 1/2.

For each lane, ucverify reports:

- `wrong_output`: an output register differs from the task's ideal, outside
  the task's tolerance band. Preserved inputs are compared exactly.
- `wrong_sign_const`: the sign does not depend on any outcome, and it is
  wrong.
- `outcome_dependent_sign`: as defined above.
- `dirty`: a qubit outside every Qubit register is nonzero at the end.

It prints up to `--list` failing lanes with their inputs and outputs, then a
summary line per run. The final line is `{"verdict": "PASS"}` or `FAIL`, and
the exit status is 0 or 1. Structural rejections (bad magic, segment rules,
truncation, unknown kinds or flags, an END count mismatch, data-gate index,
id range, operand aliasing, address caps, register schema) print the
evaluator's failure code and exit 1. ucverify does check every one of
these. It parses the whole stream before it executes any op, though, so for
a stream with more than one defect it can report a different first error
than the evaluator.

### Task adapters

Each adapter follows its task README's input and output contract.

**`hhl-angle`.** This adapter is exhaustive over all 2,031,616 in-domain
`X` in `[2^16, 2^21)`. The `--sample` option samples `X` instead. The
expected result is `k + round(2^32 asin(2^15/X)) mod 2^33`. It uses an f64
`asin` and rounds half away from zero, as the grader does. A lane passes
when `X` is unchanged and the centred residue is at most 429.

**`coulomb-potential`.** For each fixed `r_i` (`--ri`, repeatable; the
default is four points), this adapter is exhaustive over all 2^24 `r_j`.
`r_j = r_i` is skipped as out of domain. The ideal is
`F = round(2^24/sqrt(n2))`, accepted within ±32. The full domain is the 2^48
point pairs, so this adapter covers 2^24 pairs per `r_i`.

**`gaussian`.** This adapter samples `X` uniformly from the 2^36 in-domain
patterns (`--lanes`, `--seed`). With `--sweep` it uses a deterministic
low-discrepancy walk instead. The ideal is
`k + round(2^32 exp(-(X/2^32)^2)) mod 2^39`, accepted within ±429.

**`vectors:FILE`.** This is a generic adapter for the larger tasks
(chemistry, DQI, lattice, modinv). It reads inputs, expected outputs,
expected signs and per-register bands from a `UCVEC001` file. ucverify
contains no reference implementation for these tasks. The vectors come from
the task's own `gen_test_indexed`, run with a seed you choose (see below).
For these tasks, then, ucverify adds the exact sign check over all outcome
branches, the dirty-qubit check, an independent seed and any sample size.
It does not add an independent oracle.

### Accumulator patterns (`--k`)

The `--k` option sets the initial accumulator for the hhl, coulomb and
gaussian adapters. It is repeatable, with one run per mode. Write `f` for the
ideal addend and `x` for the lane's input. The modes are:

| mode | initial `k` |
|---|---|
| `zero` | 0 |
| `ones` | all ones |
| `xormix` | the top bits of `x * 0x9E3779B97F4A7C15` (Fibonacci hashing) |
| `splitmix:S` | SplitMix64 of `(S << 32) ^ x` |
| `random:S` | SplitMix64 of `SplitMix64(S) ^ x`, uniform and independent of `x` |
| `kneg:D` | `D - f`, so the ideal result is exactly `D` |

`--strict` runs the patterns zero, ones, xormix, splitmix:1, splitmix:5,
random:1–4 and kneg:0. It also runs `kneg:2^j-1` and `kneg:2^j` across the
accumulator's upper bits, which puts the ideal result at every carry
boundary.

### Making vectors for the larger tasks

ucverify reads vectors; it does not compute them. A short Python program
that calls the task's own grader writes them. Python is used here only to
run the grader. The program is reproduced below; it writes the `UCVEC001`
layout, which is:

```text
"UCVEC001" u32 n_regs
per register: u32 width, u8 kind (0 Qubit, 1 Bit), u64 band
u64 n_lanes
per lane: u8 live; if live: u8 expected_phase, then per register:
          u8 flags (1 = input present, 2 = expected present), input bytes,
          expected bytes (little-endian, ceil(width/8) bytes each)
```

```python
# dump_vectors.py REPO TASK SEED OUT RANGE...   (RANGE: a:b or n@N)
# SEED "h_ops:<hex>" reproduces the evaluator's own draws for that stream.
import hashlib, importlib.util, struct, sys
repo, task, seed, out = sys.argv[1:5]
sys.path.insert(0, repo)
from uncompute.evalcore.protocol import ShotXof
from uncompute.evalcore.task import BIT
spec = importlib.util.spec_from_file_location("grader", f"{repo}/tasks/{task}/grader.py")
mod = importlib.util.module_from_spec(spec); spec.loader.exec_module(mod)
T = mod.TASK; schema = T.schema()
bands = {"hhl-angle": {1: 429}, "gaussian": {1: 429}, "coulomb-potential": {6: 32}}.get(task, {})
if task == "lattice-gauge-plaquette":
    bands = {len(schema) - 1: mod.EPS}
shots = []
for r in sys.argv[5:]:
    if "@" in r:
        n, N = (int(v) for v in r.split("@")); shots += [i * N // n for i in range(n)]
    else:
        a, b = (int(v) for v in r.split(":")); shots += list(range(a, b))
if seed.startswith("h_ops:"):
    seed_tests = hashlib.shake_256(T.seed_domain + b"/tests/" + bytes.fromhex(seed[6:])).digest(32)
else:
    seed_tests = hashlib.shake_256(T.seed_domain + b"/tests/ucverify/" + seed.encode()).digest(32)
with open(out, "wb") as f:
    f.write(b"UCVEC001" + struct.pack("<I", len(schema)))
    for i, rs in enumerate(schema):
        f.write(struct.pack("<IBQ", rs.width, 1 if rs.kind == BIT else 0, bands.get(i, 0)))
    f.write(struct.pack("<Q", len(shots)))
    for shot in shots:
        tc = T.gen_test_indexed(shot, ShotXof(seed_tests, shot))
        if tc is None:
            f.write(b"\0"); continue
        f.write(bytes((1, 1 if tc.expected_phase else 0)))
        for i, rs in enumerate(schema):
            nb = (rs.width + 7) // 8
            vin = tc.inputs[i] if i < len(tc.inputs) else None
            vex = tc.expected[i] if i < len(tc.expected) else None
            f.write(bytes(((vin is not None) | ((vex is not None) << 1),)))
            if vin is not None: f.write(int(vin).to_bytes(nb, "little"))
            if vex is not None: f.write(int(vex).to_bytes(nb, "little"))
```

With `SEED = h_ops:<sha256 of the stream>`, the vectors are exactly the
evaluator's own test inputs for that stream. ucverify's `wrong_output`
count must then equal the grader's `wrong_output_shots`. That gives a direct
cross-check of the two implementations.

### `ref`

`ref` recomputes reported lanes from the README formula in extended
precision (`asinl` and `sqrtl`). That is a different code path from the f64
oracle. Each input line is `X k k_out` (hhl-angle) or
`rix riy riz rjx rjy rjz k k_out` (coulomb-potential). For each line, `ref`
prints the ideal addend both ways, the centred error, and whether the error
is inside the band.

## What ucverify does not check

- It does not check the large tasks' outputs independently. For chemistry,
  DQI, lattice and modinv, the expected values come from the grader's own
  `gen_test`.
- Its exhaustiveness is limited to what is stated above. hhl-angle is
  exhaustive in `X` for each listed `k` pattern, but not over all
  2^54 (X, k) pairs. coulomb-potential is exhaustive over `r_j` for each
  listed `r_i`. gaussian is sampled.
- It does not apply failure budgets. ucverify reports every failing lane.
  Whether a task's `Unified(budget)` would still accept the run is left to
  the reader. The DQI and modinv graders allow 100, 100 and 20 failed shots.
- It does not model the evaluator's measurement RNG or shot sharding. Its
  sign check is exact instead of sampled.
- It does not run circuits, only stored streams.
