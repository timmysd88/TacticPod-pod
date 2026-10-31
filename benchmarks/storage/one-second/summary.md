# Host compression benchmark results

Generated host results; synthetic capacities are projections, not hardware guarantees.

| Case | Lightweight bytes/block | Pages/block | Minutes, 14 data pages | Minutes, 15 data pages |
|---|---:|---:|---:|---:|
| stillness | 841.3 | 4.000 | 238.9 | 255.9 |
| walking | 1045.0 | 5.000 | 191.1 | 204.8 |
| sprinting | 1252.2 | 6.000 | 159.2 | 170.6 |
| jumping | 1082.0 | 5.000 | 191.1 | 204.8 |
| rapid_turns | 1111.5 | 5.000 | 191.1 | 204.8 |
| noisy | 1340.2 | 6.000 | 159.2 | 170.6 |
| adversarial_uniform | 1611.6 | 7.025 | 136.0 | 145.7 |
| real_recording | 1052.0 | 5.067 | 188.6 | 202.1 |

See results.json for every predictor/backend, exact target allocations, maxima, host timing and provenance.
