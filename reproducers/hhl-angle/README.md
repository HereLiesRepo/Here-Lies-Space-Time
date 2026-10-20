# hhl-angle: two streams the stock grader accepts that are wrong

Both streams below are graded **OK** by the unchanged grader (andigu/uncompute @ 64b5a13,
`tools/eval.py hhl-angle`), with 0 failed shots. Both are wrong for some accumulator values k.
Neither is one of our results. Both are small edits of the authors' released hhl-angle circuit.

| Reproducer | Official grade | Toffolis x qubits | Wrong on |
|---|---|---|---|
| `coverage-gap-1211` | OK, 0 of 8,744 shots failed | 1,211 x 115 = 139,265 | k_out on 6 lanes (xormix k), 1 (splitmix:1), 3 (splitmix:5), with outcome-dependent sign |
| `coverage-gap-blip` | OK, 0 of 8,749 shots failed | 1,173 x 114 = 133,722 | k_out on 2 lanes (xormix k) |

Lane counts are out of 2,031,616 inputs X per k mode, checked exhaustively over X. `exact_check.log`
in each folder holds the checker's output against the authors' 1,348-Toffoli baseline. Zero-k mode
is clean for both, which is why sampled k almost never catches them.

When we found it, the blip stream scored below the best correct stream we had for this task
(1,174 x 114 = 133,836), so the stock grader would have accepted it as a new best. Our
current hhl-angle stream is 1,087 x 113 = 122,831.

## Reproduce

    git clone https://github.com/andigu/uncompute && cd uncompute && git checkout 64b5a13 && uv sync
    xz -dc coverage-gap-blip/stream.ops.xz > blip.ops
    sha256sum blip.ops            # = grade.json h_ops
    uv run python tools/eval.py hhl-angle --ops blip.ops --json    # verdict OK

The exact per-lane (X, k) list and a standalone C checker that reports them are in the verifier
package, together with the patch to `eval.py` that rejects both streams. [pending]
