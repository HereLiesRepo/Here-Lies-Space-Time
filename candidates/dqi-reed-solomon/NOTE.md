# dqi-reed-solomon candidate (slice GU-S2): Clifford peephole only

Status: UNVERIFIED for correctness. This repository has no fast correctness check for this task. The grader has not been run on this candidate. Only the three structural counts below were measured.

## Counts (`xz -dc stream.ops.xz | verifier/ucverify count -`)

| | base `circuits/dqi-reed-solomon/stream.ops.xz` | candidate |
|---|---|---|
| Toffoli | 442,043 | 442,043 |
| qubits | 1,837 | 1,837 |
| Clifford | 124,823,890 | 124,209,420 |
| expanded ops | 125,502,410 | 124,887,940 |
| h_ops | 15be73a0... | 9dc40b38057a21c2d032801ed301aff753750e8ea6886e1c3d3ca06f9c476f10 |

Target B (Clifford <= 124,823,678 with Toffoli <= 442,043 and qubits <= 1,837) is met by these counts. Target A is not attempted.

## Edit

Stream-level only. The tool is `tools/rw.c` (C, not Python). It parses the OPSTRM01 segments and deletes adjacent pairs of identical ops, where the op is one of CX, CZ or SWAP, with flags == 0 (not bit-conditioned, not gated), and the raw encoded bytes (kind, flags, operands with bases and strides) are equal. Pairs are found with a stack, so nested cancellations are also removed. A loop body that becomes empty is dropped.

Why each edit is an identity: CX, CZ and SWAP are self-inverse, and the two ops are adjacent in the executed sequence (the same FLAT list, or the same LOOP body item list, which runs in order for each iteration). Two adjacent identical involutions compose to the identity on the state and do not touch any measurement or outcome. Outcome numbering, HMR/R ops, register declarations, DATA tables and qubit ids are unchanged, so Toffoli and qubit counts are unchanged too. END is reduced by the removed ops, weighted by enclosing loop counts. `ucverify count` accepts the output, and its op count equals the new END value.

Removed: 307,235 pairs (614,470 Cliffords). Every removed pair has loop multiplicity 1: the plain and loop-weighted pair counts are both 307,235 from the tool census.

Only 212 Clifford reductions were needed for target B, so the candidate removes far more than the target requires. If a smaller edit is preferred, the tool can be restricted to the first N pairs.

## What is not done

No wire relocation (target A needs qubits <= 1,836, not attempted). No correctness check of any kind beyond the structural count. The base grade (`grade.json`) already records 8 failed shots of 100,000 (wrong_sign_shots 8); this candidate has not been graded, so the failure count is unknown.
