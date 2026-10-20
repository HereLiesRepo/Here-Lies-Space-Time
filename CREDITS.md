# Credits

Author: Michael David Brown (HereLiesScarlet), Lafayette, LA. Contact: here.lies.scarlet@gmail.com

The circuits in this repository are measured on, and in most cases derived from, work by other people. This file names them. Each entry says what the cited work is. It does not say how much of any circuit comes from it.

## The paper and the grader

- A. Gu, A. H. Kemeny, S. F. Yelin, M. D. Lukin, arXiv:2610.22367. This is the paper whose circuits are used as the comparison. The paper reports that its divstep circuit over a prime field was found by an LLM search ("Claude Mythos 5" inside OpenEvolve) built by the authors.
- The grader, task definitions, op-stream format and the circuits labelled "discovered" are from `andigu/uncompute` at commit `64b5a13aa249f1360846657392e6c66817b26d0f`. Every grade file here comes from that tool.
- The op model follows the kickmix circuit model of Babbush et al. (2026), arXiv:2603.28846, as stated in the grader's `uncompute/ops.py`.

## Point addition on secp256k1 (`circuits/modinv-point-add`)

The inversion inside this adder is a dialog-GCD walk. Its lineage, in order of time:

- Dialog representation (record and replay of GCD branch choices): Tanuj Khattar, Noah Shutty, Craig Gidney, Adam Zalcman, Noureldin Yosri, Dmitri Maslov, Ryan Babbush, Stephen P. Jordan (Google Quantum AI), arXiv:2510.10967 (2025).
- Dialog-GCD adapted to secp256k1 point addition: Andre Schrottenloher (Inria/IRISA), arXiv:2606.02235 (2026-06-01).
- ECDSA.Fail: the public submission project (Jieyi Long and co-authors, with the contributors it credits). The comparison-free ping-pong form of the walk used here was submitted to ECDSA.Fail in commit `897dda2` by the account `teddyjfpender`, which the project describes as an agent-mediated workflow. Its Jump-2 and base-5 variants come from Joe Doyle, Trail of Bits (TrailMix), and were entered into ECDSA.Fail by the account `tob-joe`. Handle-to-person links other than those the project states are name matches only.
- Bound on the number of divstep iterations (590 for 256-bit inputs): Pieter Wuille, Gregory Maxwell and Russell O'Connor (Coq proof), building on the divsteps of Daniel J. Bernstein and Bo-Yin Yang (TCHES 2019).
- Adders: the stream in this repository uses Gidney's measurement-based adder (C. Gidney, "Halving the cost of quantum addition", Quantum 2, 74, 2018).
- Half-squares in the multiplications of the published-techniques adder: ECDSA.Fail Stage 4.
- Squares in the current entry: every sub-square of its Karatsuba squares is an exact square written from the controlled add-subtract and schoolbook sections of D. Litinski, "Quantum schoolbook multiplication with fewer Toffoli gates", arXiv:2410.00899 (2024). No ECDSA.Fail source was used for them.
- Binary GCD step: J. Stein (1967), as cited by the TrailMix notes.

Source for this table: the division-provenance review we carried out. Items that review marked unresolved (who wrote the specification and evaluator of the paper, the exact origin of Jump-2 and base-5, handle-to-person links) remain unresolved here.

## Function-evaluation tasks (`hhl-angle`, `gaussian`, `coulomb-potential`)

- Baseline in the task READMEs: T. Haner, M. Roetteler, K. M. Svore, "Optimizing quantum circuits for arithmetic", arXiv:1805.12445 (2018).
- The streams here are edits of the paper authors' released circuits (`tasks/<task>/circuits/discovered` in the grader). The edits are described in the white paper.

## Other tasks

- `chemistry-fe2s2`: baseline `babbush-composed` in the grader. The stream here is an edit of the authors' discovered circuit (restructured lookup stage, then a further stream rewrite).
- `lattice-gauge-plaquette`: baseline `osorio-composed` in the grader. The stream here is an edit of the authors' discovered circuit.
- `dqi-hermitian`: baseline `sakata-fengrao` in the grader (Sakata, Jensen and Hoholdt, 1995). The stream here is the authors' discovered circuit with the walk rebuilt and other edits described in the white paper.
- `dqi-reed-solomon`: baseline `khattar-eea` in the grader (Khattar et al., 2025). The stream here is an edit of the authors' discovered circuit: selected Toffolis replaced, then high wire ids relocated into wires that are zero at that point.

## Tooling

The circuits were produced with private tooling that is not part of this repository. The OpenQASM 3 files were written from the op streams by a separate converter that is also not included. The grade files and the op streams are the primary record.
