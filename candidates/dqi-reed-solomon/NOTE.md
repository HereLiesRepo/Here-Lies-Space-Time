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

# dqi-reed-solomon: qubits 1,837 -> 1,836 (GU-S3), not achieved

Start stream: `circuits/dqi-reed-solomon/stream.ops.xz`. `ucverify count` gives
toffoli 442,043, clifford 124,823,890, qubits 1,837, ops 125,502,410 (matches grade.json).

## Liveness of the top wire (id 1836)

- The top id is 1836, so qubits = 1,837 comes from this one wire. It is not a register wire.
- Operand hits on wire 1836: 113,869 (113,704 CX controls, 64 CX targets, 32 CCX controls,
  68 CCX targets, one R). Of the 15,613 hit op nodes, 24 sit inside loops; the rest are literal ops.
- Its only reset is an R, at T = 193,916,231 (T counts every expanded op, gated-off ones included).
  It is never measured (no HMR). Its first touch is at T = 14,285,372. Hits are spread
  evenly across that span, roughly 11,400 per 20M ops.
- Relocating it therefore needs one wire that is (a) idle over T in [14,285,372, 193,916,231],
  and (b) provably zero at T = 14,285,372.

## Search result

- Non-register wires below 1836 with no operand touch in that window: 495.
- Their last touch before the window is CX, CZ, X, Z or SWAP in every case. None is an R/HMR
  reset, and none is untouched before the window. So no idle wire is provably zero at window start.
- A wire that is zero only on the tested inputs would not be a proof. Correctness can't be checked
  here (no checker for this task), so I did not edit the stream.

## Not tried

- Splitting the window into shorter episodes: wire 1836 has no reset between its first touch and the
  final R, so there is nothing to split on.
- Relabelling by swapping two wires' whole timelines: any lower wire idle in the window must
  already be unused there, which the 495 idle wires are not.
- A symbolic or simulated zero-analysis of the 495 candidates. That could narrow the search, but
  it would prove zero only for the simulated inputs.
