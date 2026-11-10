# arXiv:2610.22367 — every headline metric vs SpaceTime (interim, 2026-10-27)

Status: INTERIM. Built from this repo's `grade.json` receipts and the paper's text (Table I, Extended Data Table 1, Table S3, SI I.1). The grader repo `andigu/uncompute` could not be attached in this session (add_repo was denied), so no paper circuit was re-graded here and no search was run. Nothing below was run through a "stricter" grader in this lane.

## What the paper actually reports
The paper's main table (Table I / Extended Data Table 1) has these columns only: Toffoli count T, peak qubits Q, TQ-cost (T x Q), plus correctness tolerance and test counts. It reports **no gate count, no T-gate count (Toffoli+CCZ counted equally), no depth, no Clifford count**. The paper says depth is not its objective (Toffoli depth is only mentioned as an alternative cost model).

Claims in the brief that I could not source:
- **"17x gate reduction on modular inversion": not found** in the paper text. Its modular-inversion row has no baseline ("—"); Table S3 compares four constructions (Schrottenloher 1.9M T / 1,446 Q; divsteps 4.0M / 935; Luo 70M / 838; specialised Euclidean 21M / 843). Ratios there: Luo -> divsteps = 17.4x in Toffolis (7.0e7 / 4.0e6), which is probably the origin, but it is a ratio against Luo, not against a "state of the art", and divsteps uses *more* Toffolis than Schrottenloher.
- **"12x cut on the decoder's costliest step"**: the paper's 12x is the *whole-circuit* Reed-Solomon Toffoli count (5.1M -> 4.4e5) and TQ-cost, not a single step. Its Euclidean-pass step figures are: scan 1.3M (baseline) -> 93k; EEA 44k each direction.

## Table (one row per task x metric)
Paper = discovered circuit as printed in the paper (rounded where the paper rounds). Ours = this repo's `grade.json`. Status per the lane rules: BEATEN needs authors' grader + stricter grader receipts; BOUND needs a proof; else OPEN.

| task | metric | paper | ours | status | note |
|---|---|---:|---:|---|---|
| hhl-angle | Toffoli | 1,348 | 1,087 | BEATEN (authors' grader) | receipt `circuits/hhl-angle/grade.json`; stricter-grader receipt: `patched_grade.json` only (coverage patch), no independent strict run here |
| hhl-angle | qubits | 115 | 113 | BEATEN (authors' grader) | same receipts |
| hhl-angle | Clifford | 56,419 (verifier README) | 33,532 | BEATEN (authors' grader) | paper figure is from `verifier/README.md` example, not the paper |
| hhl-angle | depth / T-gates / gate total | not reported | n/a | OPEN | grader emits no depth; ops total 37,018 ours, paper ops unknown |
| gaussian | Toffoli | 1,583 | 1,416 | BEATEN (authors' grader) | |
| gaussian | qubits | 139 | 138 | BEATEN (authors' grader) | |
| coulomb-potential | Toffoli | 1,140 | 1,012 | BEATEN (authors' grader) | |
| coulomb-potential | qubits | 142 | 139 | BEATEN (authors' grader) | |
| chemistry-fe2s2 | Toffoli | 14,978 | 9,467 | BEATEN (authors' grader) | |
| chemistry-fe2s2 | qubits | 126 | 145 | OPEN | ours is 19 higher; no qubit-reduction attempt recorded in this repo |
| dqi-hermitian | Toffoli | 926,912 | 707,278 | BEATEN (authors' grader) | |
| dqi-hermitian | qubits | 2,395 | 2,328 | BEATEN (authors' grader) | |
| dqi-hermitian | failed shots (of 100,000) | 30 (paper text) | 59 | OPEN | ours is worse; limit is 100 |
| dqi-reed-solomon | Toffoli | 442,397 | 442,043 | BEATEN (authors' grader) | margin 0.08% |
| dqi-reed-solomon | qubits | 1,837 | 1,837 | OPEN | tie, not beaten |
| dqi-reed-solomon | failed shots | 7 (paper text) | 8 | OPEN | ours is worse; limit is 100 |
| lattice-gauge-plaquette | Toffoli | 172,128 | 152,832 | BEATEN (authors' grader) | |
| lattice-gauge-plaquette | qubits | 1,859 | 1,850 | BEATEN (authors' grader) | |
| modinv-point-add | Toffoli | 4,006,673 | 999,283 | BEATEN (authors' grader) | 4.0x fewer than the paper's divsteps circuit; 7/20 failures allowed |
| modinv-point-add | qubits | 935 | 1,317 | OPEN | ours is 382 higher |
| modinv-point-add | Toffoli vs Schrottenloher (1.9M, 1,446 Q) | 1.9e6 | 999,283 | OPEN | Schrottenloher is not a circuit committed here or graded here |
| all 8 | TQ-cost | see README | see README | BEATEN (authors' grader) | the original claim; not recounted |
| all 8 | Clifford (other 7) | needs grader results.jsonl | in `grade.json` | OPEN | cannot compare without paper-circuit grades |

Counts: BEATEN 14 + 1 (TQ row), BOUND 0, OPEN 12 (rows as written above; "BEATEN" here means authors' grader only).

## What is blocked
- Re-grading the paper's circuits (Clifford, ops) and any stricter-grader run need `andigu/uncompute@64b5a13`; attaching it was denied in this session. Needs the owner's call.
- No BOUND rows: nothing in the repo is a lower-bound or optimality proof, and none was found.
- No search was run (none of the OPEN rows has "what was tried" beyond "not attempted in this lane").
