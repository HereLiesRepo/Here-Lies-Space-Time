# ucverify timing (single core, this container, 2026-10-27)

| task | `count` | `check` (full correctness) | lanes |
|---|---:|---:|---:|
| hhl-angle | 0.02 s | 21.0 s | 2,031,616 |
| gaussian | 0.02 s | 55.1 s | 4,194,304 |
| coulomb-potential | 0.01 s | 415.9 s | 16,777,216 |
| chemistry-fe2s2 | 0.82 s | not implemented ("unknown task") | - |
| lattice-gauge-plaquette | 2.09 s | not implemented | - |
| dqi-*, modinv-point-add | not timed | not implemented | - |

Time goes into lane simulation (one lane per input), not parsing: counting a 600k-op stream takes under a second, checking hhl-angle takes 21 s. `check` exists only for hhl-angle, gaussian and coulomb-potential. For the other five tasks C gives counts only, so a search there has no fast correctness oracle; the official grader (master's container) is the only checker.
