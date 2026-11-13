# Our verifier on every committed circuit (ucverify, 2026-11-13)

Run at repo commit `c90e715954bcbd1e1c944f9ec992154372b9dd65` with `verifier/ucverify` built by `make -C verifier` (`cc -std=c11 -O2 -Wall -Wextra -Werror`), each stream piped through `xz -dc` from `circuits/<task>/stream.ops.xz`:

`ucverify check <task> - --strict` where the task has an adapter, and `ucverify count` (the stream's independent Toffoli, Clifford and qubit census, SHA-256 `h_ops`) for all eight.

| task | `check --strict` | lanes run | counts from `ucverify count` (Toffoli / qubits) | `h_ops` (SHA-256 of the stream) |
|---|---|---|---|---|
| hhl-angle | PASS (89 s) | 2031616 per run | 1087 / 113 | `2c59af9e93cd947ee7734d731a87039cc9a12561f0e122f071531a9508455985` |
| gaussian | PASS (302 s) | 4194304 per run | 1416 / 138 | `c9ca88774d4849c7b61601ab7629606f86fced2b15a9bed528b5880845582ad1` |
| coulomb-potential | PASS (1313 s) | 16777216 per run | 1012 / 139 | `35295aa5d6aaa9861cae195959b8baebf1f9b40ee26c39416fdcace068c3cbe3` |
| chemistry-fe2s2 | no adapter (unknown task) (0 s) | n/a | 9467 / 145 | `fe4ba65050aa999d365f793885f5b6af530813b90e5125e2caeff9e116c79501` |
| lattice-gauge-plaquette | no adapter (unknown task) (1 s) | n/a | 152832 / 1850 | `291fb64deb732c0a2bb25b7f98d8a03b2ecec4eff3f40b8ffd8eeaac0cbdfc13` |
| dqi-hermitian | no adapter (unknown task) (26 s) | n/a | 707278 / 2328 | `5d58460db308fcce019abe9a1c6336e1391e032b9cf4ab5a33c23986363f6860` |
| dqi-reed-solomon | no adapter (unknown task) (12 s) | n/a | 442043 / 1837 | `15be73a0ab99d4ac4694e2aa9bff31d0ac9be5dfd3abd68854be5e56bd44466b` |
| modinv-point-add | no adapter (unknown task) (3 s) | n/a | 999283 / 1317 | `394a96b03e0e05c1627340912a1292ed1cfe835b48d04733967d5f6f5e057b42` |

What this is and is not:

- PASS on hhl-angle, gaussian and coulomb-potential is our stricter checker (`--strict` tracks every measurement outcome's effect on the sign exactly, which the evaluator's random draw catches with probability 1/2): zero `wrong_output`, `wrong_sign_const`, `outcome_dependent_sign`, `reset_of_nonzero` and `dirty` lanes. hhl-angle is exhaustive over all 2,031,616 inputs for every k mode; coulomb-potential is exhaustive over 2^24 `r_j` for each default `r_i` (not the full 2^48 pairs); gaussian samples `X` (see `verifier/README.md`).
- For chemistry-fe2s2, lattice-gauge-plaquette, dqi-hermitian, dqi-reed-solomon and modinv-point-add `ucverify` has no output adapter (`unknown task`): the grade of those rests on the authors' grader receipts in `circuits/*/grade.json`. The counts above are independent and equal those receipts' Toffoli and qubit figures.
- Raw outputs are not committed (the hhl-angle and coulomb runs print one JSON line per k mode or `r_i`); rerun: `xz -dc circuits/<task>/stream.ops.xz | verifier/ucverify check <task> - --strict`.
- This establishes grades, not optimality: no BOUND row is added.
