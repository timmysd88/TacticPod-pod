# Existing 16 MiB flash: lossless storage experiment

Experimental host benchmark only. No active firmware or frozen V2 specification changes.

Seven deterministic 120-second synthetic cases plus 3,000 real CSV-derived IMU samples.
The complete 3,026-sample real source, including its partial tail, is round-trip tested.
GPS is synthetic in every row. Capacity is normalised to 100Hz IMU, not the real log's slower arrival rate.

| Case | 1s aligned, 14 pages | 2s aligned, 14 pages | 2s stream, 14 pages | 2s stream, 15 pages |
|---|---:|---:|---:|---:|
| adversarial_uniform | 136.0 min | 136.5 min | 143.2 min | 153.4 min |
| jumping | 191.1 min | 212.3 min | 219.7 min | 235.3 min |
| noisy | 159.2 min | 172.7 min | 174.8 min | 187.3 min |
| rapid_turns | 191.1 min | 211.1 min | 213.9 min | 229.1 min |
| real_recording | 188.6 min | 210.8 min | 224.5 min | 240.5 min |
| sprinting | 159.2 min | 173.7 min | 188.0 min | 201.4 min |
| stillness | 238.9 min | 273.0 min | 289.1 min | 309.7 min |
| walking | 191.1 min | 212.3 min | 228.8 min | 245.2 min |

Durations repeat the measured block-size sequence to flash capacity. They are not endurance tests or guarantees.
All block headers, block CRCs, page CRC/markers, page padding, sector headers, final-sector rounding and the diagnostic sector are counted.
15-page stream projections additionally reserve 256 payload bytes for a future clean-end frame; its command/format is not implemented.
15-page sectors require a new format with an appended session-end frame instead of the existing per-sector reserved footer.

## Real recording: codec comparison, two-second blocks

| Codec | Encoded bytes/block | Aligned pages/block | Aligned capacity | Mean host encode time |
|---|---:|---:|---:|---:|
| v2_raw+none | 5796.0 | 25.000 | 76.4 min | 0.168 ms |
| block_u32+none | 4076.0 | 18.000 | 106.2 min | 0.173 ms |
| time_delta+none | 3653.9 | 16.000 | 119.4 min | 0.238 ms |
| time_dod+none | 3475.5 | 15.000 | 127.4 min | 0.203 ms |
| delta_varint+none | 2732.9 | 12.200 | 156.6 min | 0.566 ms |
| dod_varint+none | 2672.7 | 11.800 | 161.9 min | 0.569 ms |
| delta_bitpack+none | 2430.7 | 11.000 | 173.7 min | 0.482 ms |
| dod_bitpack+none | 2465.7 | 11.000 | 173.7 min | 0.539 ms |
| adaptive_rice+none | 1975.0 | 9.067 | 210.8 min | 5.360 ms |
| adaptive_rice+lzss1k | 1975.0 | 9.067 | 210.8 min | 6.476 ms |
| adaptive_rice+deflate512 | 1944.3 | 8.800 | 217.1 min | 5.491 ms |
| adaptive_rice+deflate32k | 1921.3 | 8.733 | 218.8 min | 5.360 ms |

CPython timings are not nRF52840 timings; candidates are explored offline. Full DEFLATE32k is a comparison, not a recommended embedded encoder.
The adaptive lightweight oracle excludes both DEFLATE backends and stores its selected codec ID in each block.

See [design and interpretation](../../docs/storage-compression-review.md) and each results.json for source hashes, maxima, target allocations and per-codec evidence.
