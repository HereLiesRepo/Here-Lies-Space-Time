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
