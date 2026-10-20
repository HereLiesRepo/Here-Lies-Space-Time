# HereLiesScarlet SpaceTime

By Michael David Brown (HereLiesScarlet), Lafayette, LA. Contact: here.lies.scarlet@gmail.com

Quantum circuits (op streams, OpenQASM 3 and grade receipts) that score below the circuits of arXiv:2610.22367 (A. Gu, A. H. Kemeny, S. F. Yelin, M. D. Lukin) on all eight of its tasks. Grader: `andigu/uncompute` at commit `64b5a13aa249f1360846657392e6c66817b26d0f`, unmodified.

This repository holds the best circuit for each task, a second point adder built only from published techniques, their grade receipts, the white paper (`paper/`) and a standalone C verifier (`verifier/`). It holds no generator code.

## Results

TQ-cost is the Toffoli count times the peak qubit count, as the grader defines it. "Paper" is the circuit labelled `discovered` in the grader's `tasks/<task>/circuits/`. All numbers are as the grader printed them (`grade.json`). The change column is `(this TQ / paper TQ) - 1`, computed here.

| task | Toffoli | qubits | TQ-cost | failed shots (limit) | paper Toffoli | paper qubits | paper TQ-cost | TQ change |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| `hhl-angle` | 1,087 | 113 | 122,831 | 0 (none) | 1,348 | 115 | 155,020 | -20.76% |
| `gaussian` | 1,416 | 138 | 195,408 | 0 (none) | 1,583 | 139 | 220,037 | -11.19% |
| `coulomb-potential` | 1,012 | 139 | 140,668 | 0 (none) | 1,140 | 142 | 161,880 | -13.10% |
| `chemistry-fe2s2` | 9,467 | 145 | 1,372,715 | 0 (none) | 14,978 | 126 | 1,887,228 | -27.26% |
| `dqi-hermitian` | 707,278 | 2,328 | 1,646,543,184 | 59 (100 of 100,000) | 926,912 | 2,395 | 2,219,954,240 | -25.83% |
| `dqi-reed-solomon` | 442,043 | 1,837 | 812,032,991 | 8 (100 of 100,000) | 442,397 | 1,837 | 812,683,289 | -0.08% |
| `lattice-gauge-plaquette` | 152,832 | 1,850 | 282,739,200 | 0 (none) | 172,128 | 1,859 | 319,985,952 | -11.64% |
| `modinv-point-add` | 999,283 | 1,317 | 1,316,055,711 | 7 (20 of 100,000) | 4,006,673 | 935 | 3,746,239,255 | -64.87% |
| `modinv-point-add` (published techniques only, `published/`) | 1,498,991 | 1,505 | 2,255,981,455 | 0 (20 of 100,000) | 4,006,673 | 935 | 3,746,239,255 | -39.78% |

Scope: these results are under arXiv:2610.22367's grader (every Toffoli billed on every shot, at most 2e-4 failures). The ECDSA.Fail benchmark scores differently (average billing of classically-conditioned Toffolis, submitter-chosen test inputs, zero failures on 9,024 shots); its leader scores lower under those rules, and our streams are not records there.

Every row has verdict `OK` in its `grade.json`, and the SHA-256 of the raw op stream equals the `h_ops` in that file.

Notes on the table:

- `modinv-point-add` uses fewer Toffolis than the paper circuit and more qubits (1,317 against 935). It is lower on TQ-cost only.
- `chemistry-fe2s2` uses fewer Toffolis and 19 more qubits than the paper circuit.
- `gaussian` uses one qubit fewer. `hhl-angle`, `coulomb-potential` and `lattice-gauge-plaquette` use 2, 3 and 9 qubits fewer than the paper circuit.
- Prior-art baselines named in the grader READMEs, for scale (Toffoli / qubits / TQ-cost as listed there): `hhl-angle`, `gaussian`, `coulomb-potential` use Haner-Roetteler-Svore piecewise evaluation: 16,468 / 300 / 4,940,400, then 23,784 / 362 / 8,609,808, then 6,355 / 252 / 1,601,460; `chemistry-fe2s2` babbush-composed 55,473 / 139 / 7,710,747; `dqi-hermitian` sakata-fengrao 104,263,342 / 194,053 / 2.02x10^13; `dqi-reed-solomon` khattar-eea 5,125,070 / 1,938 / 9.93x10^9; `lattice-gauge-plaquette` osorio-composed 1,271,424 / 1,927 / 2,450,034,048; `modinv-point-add` luo-eea 69,529,757 / 838 / 58,265,936,366.
- The grader reseeds its tests from `h_ops`, so the number of live shots differs per stream. It is in each `grade.json` (`shots`).

## What each entry is

Only `modinv-point-add` is a different construction from the paper's circuit. The other seven are edits of the paper authors' released circuits, so they improve on that circuit and do not replace it.

| task | source of the stream |
|---|---|
| `hhl-angle` | A rewrite of the authors' released circuit, checked exhaustively over every input X in the task's domain under several structured accumulator patterns. Methods: see the white paper. |
| `gaussian` | A rewrite of the authors' released circuit. Methods: see the white paper. |
| `coulomb-potential` | A rewrite of the authors' released circuit, checked exhaustively over all 2^24 inputs. Methods: see the white paper. |
| `chemistry-fe2s2` | A rewrite of the authors' released circuit (19 more qubits, fewer Toffolis). Methods: see the white paper. |
| `dqi-hermitian` | A rewrite of the authors' released circuit. Methods: see the white paper. |
| `dqi-reed-solomon` | The authors' released circuit with 354 Toffolis removed, each proven not to change the state for any input. |
| `lattice-gauge-plaquette` | A rewrite of the authors' released circuit. Methods: see the white paper. |
| `modinv-point-add` | An independent construction: a ping-pong dialog-GCD point adder built with our own compiler (prior work credited in CREDITS.md). Every sub-square of its Karatsuba squares is an exact square written from the controlled add-subtract and schoolbook description of Litinski, arXiv:2410.00899, with each row's carry-in made from X gates (X + Y + 1 = ~(~X + ~Y)). Compiler not published; methods in the white paper. |
| `modinv-point-add/published` | A second point adder built only from published techniques (ECDSA.Fail's ping-pong division and six-stage addition, Gidney's adders), released so that anyone can grade it. |

## Failure margins

The point adder's `grade.json` shows `wrong_output_shots` 7, `wrong_sign_shots` 3 and `dirty_qubits` 387 with `failed_shots` 7 and verdict `OK`; the limit is 20 failed shots in 100,000. On a held-out set of 100,000 inputs drawn from an independent seed it fails on 8 inputs, the same 8 that the earlier versions it was built from fail there. `dqi-hermitian` fails 59 and `dqi-reed-solomon` 8 of a limit of 100. A sampled pass is evidence, not proof.

## Verify a result

You need `andigu/uncompute` at `64b5a13`. From its root, with its environment set up (the C kernel is optional and gives identical results):

```
cd circuits/<task>
sha256sum -c SHA256SUMS
xz -dc stream.ops.xz > s.ops
sha256sum s.ops                      # must equal "h_ops" in grade.json
cd <path to andigu/uncompute>
python tools/eval.py <task> --ops <path>/s.ops --json
```

The JSON printed should equal `grade.json` except for the `timing` field. A `modinv-point-add` grade takes about ten minutes on four cores.

To check every stored stream against its `grade.json` without Python, and to re-run the verifier's controls on the authors' circuits:

```
cd verifier
make
make check UNCOMPUTE=<path to andigu/uncompute> STORE=../circuits
```

It prints `check-counts: 17 paper circuits match results.jsonl`, `check-store: 9 stored streams match their grade.json` and `make check: all checks passed`. `ucverify` counts the stream itself; it is not the official grader. See `verifier/README.md`.

`COMMITMENTS.sha256` lists the `h_ops` of every stream in one place.

## The paper

`paper/HereLiesScarlet_I_Did_Not_Read_the_README.pdf` describes the results and every technique used.

## Files

Each `circuits/<task>/` holds:

| file | content |
|---|---|
| `stream.ops.xz` | the OPSTRM01 op stream, compressed with `xz` |
| `circuit.qasm.xz` | the same circuit as OpenQASM 3, compressed with `xz` |
| `grade.json` | the grader's `--json` output, copied unchanged |
| `SHA256SUMS` | SHA-256 of the files above (`sha256sum -c`) |

| entry | `stream.ops.xz` | `circuit.qasm.xz` | `grade.json` | raw `h_ops` (SHA-256 of the decompressed stream) |
|---|---:|---:|---:|---|
| `hhl-angle` | 39,744 | 36,384 | 389 | `2c59af9e93cd947ee7734d731a87039cc9a12561f0e122f071531a9508455985` |
| `gaussian` | 21,932 | 31,900 | 397 | `c9ca88774d4849c7b61601ab7629606f86fced2b15a9bed528b5880845582ad1` |
| `coulomb-potential` | 13,120 | 16,412 | 387 | `35295aa5d6aaa9861cae195959b8baebf1f9b40ee26c39416fdcace068c3cbe3` |
| `chemistry-fe2s2` | 334,532 | 513,976 | 405 | `fe4ba65050aa999d365f793885f5b6af530813b90e5125e2caeff9e116c79501` |
| `dqi-hermitian` | 1,221,260 | not stored | 416 | `5d58460db308fcce019abe9a1c6336e1391e032b9cf4ab5a33c23986363f6860` |
| `dqi-reed-solomon` | 15,611,632 | not stored | 414 | `15be73a0ab99d4ac4694e2aa9bff31d0ac9be5dfd3abd68854be5e56bd44466b` |
| `lattice-gauge-plaquette` | 128,460 | 223,512 | 409 | `291fb64deb732c0a2bb25b7f98d8a03b2ecec4eff3f40b8ffd8eeaac0cbdfc13` |
| `modinv-point-add` | 5,126,240 | 6,923,536 | 417 | `394a96b03e0e05c1627340912a1292ed1cfe835b48d04733967d5f6f5e057b42` |
| `modinv-point-add/published` | 2,941,648 | not stored | 416 | `88be2e5777494671e2227d274ce9cc710348b75e0c7557adc36b8e8c41d9e714` |

Sizes are in bytes. QASM is not stored for `dqi-hermitian` and `dqi-reed-solomon` (several GB uncompressed) or for the published-techniques adder.

## The OpenQASM files

The grader has no OpenQASM exporter. These files come from a separate converter written for this purpose and not included here. The mapping from op kinds, with `q` the qubits, `m` the measurement outcomes and `b` the classical bits:

| op | OpenQASM 3 |
|---|---|
| X, Z, CX, CZ, SWAP, CCX | `x`, `z`, `cx`, `cz`, `swap`, `ccx` from `stdgates.inc` |
| CCZ | `ccz`, defined in the file as `h c; ccx a, b, c; h c;` |
| R | `reset q[i];` |
| HMR | `h q[i]; m[o] = measure q[i]; reset q[i];` |
| NEG | `gphase(pi);` |
| OXOR | `m[a] = m[a] ^ m[b];` |
| BIT_INVERT, BIT_STORE0, BIT_STORE1 | `b[i] = !b[i];`, `b[i] = 0;`, `b[i] = 1;` |
| outcome condition on Z, CZ, NEG | `if (m[i]) ...` |
| bit condition on X, Z, CX, CZ, NEG and the bit ops | `if (b[i]) ...` (both: `if (m[i] && b[j]) ...`) |

Loops and data-gated ops in the stream are written out in full, one statement per op. Qubit ids are the stream's fixed wire ids, and qubit reuse is expressed by `reset`. Header comments list the stream's registers. The qubit register contents and the `b[]` bits of a bit register are inputs that the grader loads per shot; the QASM does not initialise them.

As in the grader, a bit-conditioned CX or CZ is billed as a Toffoli, and a bit-conditioned X or Z as a Clifford gate. For every stored `circuit.qasm.xz`, the count of each gate type in the QASM equals the count in the op stream, and the Toffoli, Clifford, op and peak-qubit counts derived from the QASM text equal the `toffoli`, `clifford`, `ops` and `qubits` metrics in `grade.json`. The QASM files were checked for these counts only; they have not been run through an OpenQASM 3 parser or simulator.

## License

The circuits, grade receipts, OpenQASM files and the paper are released under CC BY 4.0 (`LICENSE`): reuse them freely with credit to Michael David Brown (HereLiesScarlet). `verifier/ucverify.c` and its Makefile are MIT (`verifier/LICENSE`). `verifier/uncompute_hhl_k_coverage.patch` is a change to `andigu/uncompute`, which is Apache-2.0, and is offered under that project's license.

## Not included

- The compiler, generators and search tools.
- Earlier graded versions of these circuits.
