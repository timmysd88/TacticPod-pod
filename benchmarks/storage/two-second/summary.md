# Host compression benchmark results

Generated host results; synthetic capacities are projections, not hardware guarantees.

| Case | Lightweight bytes/block | Pages/block | Minutes, 14 data pages | Minutes, 15 data pages |
|---|---:|---:|---:|---:|
| stillness | 1533.7 | 7.000 | 273.0 | 292.5 |
| walking | 1937.4 | 9.000 | 212.3 | 227.5 |
| sprinting | 2358.1 | 11.000 | 173.7 | 186.1 |
| jumping | 2018.3 | 9.000 | 212.3 | 227.5 |
| rapid_turns | 2072.8 | 9.050 | 211.1 | 226.2 |
| noisy | 2536.5 | 11.067 | 172.7 | 185.0 |
| adversarial_uniform | 3096.6 | 14.000 | 136.5 | 146.2 |
| real_recording | 1975.0 | 9.067 | 210.8 | 225.8 |

See results.json for every predictor/backend, exact target allocations, maxima, host timing and provenance.
