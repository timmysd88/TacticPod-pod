# Existing 128 Mbit / 16 MiB flash: lossless compression investigation

Host-only experiment, 2026-10-30. The existing JEDEC 0B4018/SFDP-confirmed chip
is **128 Mbit = 16 MiB = 16,777,216 bytes**, not 128 MB. No active firmware,
flash writer, frozen V2 specification or recording data is changed. No serial
port is opened. TCB0/TPC0/TPS0 below are experimental host containers, not a new
approved on-device format.

## Evidence and reproduction

- [Benchmark implementation](../tools/storage_benchmark.py): Python 3.10+ standard library.
- [Automated tests](../tests/storage_benchmark/run_tests.py).
- [Result overview](../benchmarks/storage/summary.md).
- [One-second full matrix](../benchmarks/storage/one-second/results.json).
- [Two-second full matrix](../benchmarks/storage/two-second/results.json).

Each run tests nine predictors/packers with four compression backends, so 36
combinations per block. Seven deterministic 120-second synthetic cases are used:
stillness, walking, sprinting, jumping, rapid turns, noisy movement, and independent
uniform full-range i16 data. Fixed seeds and canonical input SHA-256s are recorded.
One- and two-second runs MUST have identical canonical source hashes; block size
does not alter the input GPS noise, sensor data or timestamps.

The synthetic motion traces use rounded/clamped sinusoidal/harmonic raw values,
Gaussian noise and jump/turn transients. They are illustrative stress cases, not
validated biomechanical/LSM6DS3 noise models. The stillness noise assumption is
deliberately optimistic: 4 raw accelerometer counts and 12 gyro counts standard
deviation. Walking uses 20/20, sprinting 80/60, jumping 30/30, turns 40/40 and noisy
movement 500/700. Sensor saturation is retained as the simulated raw reading;
no decoder smoothing or precision reduction occurs.

Synthetic IMU timestamps include +/-200us jitter and a 2300us delay every 137
samples. RMC arrives at 10Hz with +/-1000us receive-time variation, moving/noisy
coordinates, speed and course. GGA arrives at 1Hz with varying HDOP, satellites
and altitude. Invalid RMC status and missing UTC are included. All GPS fields are
the frozen V2 fixed-point fields, not compressed strings or stale cached values.

### Real data found in the workspace

`logs/device-monitor-261027-204904.log` contains a real V1 RECORD READ CSV:
**3,026 six-axis samples spanning 30.872 seconds**, one session. Its motion is
unlabelled and its raw ranges show substantial movement; it is not called a
stillness trial. Timestamp gaps are 10ms x2713, 11ms x95, 12ms x185, 13ms x16,
14ms x1 and 17ms x15. All original timestamps are preserved.

All six channels map unambiguously back to i16 counts at the recorded sensitivities
61 micro-g/LSB and 8750 micro-degrees/s/LSB. For every field, exactly one nearby
integer reproduces the observed six-decimal text through the firmware's float32
conversion within the printing tolerance. This is inferred reconstruction from
CSV, not independent verification against the source flash page bytes/CRCs.

Full-block capacity comparisons use 3,000 samples; the remaining 26 samples are
also round-trip tested as a partial tail. Capacity is normalised to **100 samples
per second**, not inflated by the real source's approximately 98Hz arrival rate.
The V1 time precision is milliseconds; conversion to microseconds is exact but
does not recover missing sub-millisecond timing. New Phase 2 microsecond timestamps
may cost more to encode. The real source contains **no GPS**: synthetic GPS is
combined with it and is labelled as such in every report. No new hardware data
has been collected.

## Methods and exact reconstruction

The immutable benchmark Record contains type, original u64 timestamp and all
original payload integers. Canonical comparison serialises the complete frozen
V2 record bytes, including mask/status/date/time fields. Both scalar values and
cross-stream ordering, including equal-timestamp ordering, must compare exactly.

| Predictor/packing | Method |
|---|---|
| v2_raw | Complete V2 wire records inside an experimental block |
| block_u32 | Block base u64; exact u32 offsets; grouped raw payloads; 2-bit original type/order map |
| time_delta | Exact unsigned timestamp deltas per stream; raw measurement payloads |
| time_dod | ZigZag/varint delta-of-delta timestamps; raw measurement payloads |
| delta_varint | Per-column first raw value then ZigZag/varint differences |
| dod_varint | Per-column two raw seeds then ZigZag/varint second differences |
| delta_bitpack | First raw value and exact signed deltas packed at their required width |
| dod_bitpack | Two raw seeds and exact signed second differences packed at their required width |
| adaptive_rice | Per-column shortest raw/varint/bit-packed/escaped Rice representation; exact adaptive timestamp coding |

The adaptive timestamp encoder chooses direct deltas, delta-of-delta, an exact
GCD factor plus second differences, or runs of **actually zero** second differences.
No nominal 10ms timestamp is substituted for a measured value. Every exception
and nonzero residual is stored. An irregular timestamp sequence round-trips just
like a regular one. GPS columns retain the full latitude E7, longitude E7,
speed mm/s, course E5, UTC, masks, status, mode and talker bytes. GGA retains its
full quality fields and independent timestamp. Missing/invalid fields remain
missing/invalid; compression never upgrades their validity.

Rice quotient is capped at 31 before an exact fixed-width escape, preventing
unbounded unary scans. Signed differences use wider arithmetic; i16 transitions
from -32768 to 32767 are not truncated. Event u64 extrema are tested too.

Each transformed body is tested with:

- No dictionary compression.
- Custom LZSS: 1KiB history, at most eight match candidates, lengths 3–18 bytes.
- DEFLATE level 1, 512-byte window / memory level 1.
- DEFLATE level 1, 32KiB window / memory level 8 (reference comparison).

Compression is used only when smaller, and every combination has an unconditional
raw fallback. The adaptive lightweight result selects the shortest available
non-DEFLATE block, with its selection encoded in the header. **In these runs,
adaptive_rice without LZSS was selected for every block.** Searching the whole
matrix is an offline comparison, not a proposal to run 36 compressors on the MCU.
Further LZ compression gives little benefit after useful redundancy is removed.

Raw body, encoded body and recovery buffers are bounded at 8192 bytes plus the
36-byte block envelope. At most 256 records fit a block; the two-second nominal
case has 222 records. Header sizes/counts are checked before allocation/decoding.
All block/stream decompression must finish at exactly its declared length, with
no trailing bytes, overflow, invalid padding or unbounded varint/Rice input.

## Flash allocation: exactly what is counted

The existing chip has one 4096-byte diagnostic sector excluded from recording,
leaving **4095 sectors / 16,773,120 recording bytes**. Existing page integrity
positions are retained in both experimental page containers: 16-byte envelope,
232-byte payload, CRC at 248, marker at 252.

An experimental block has a 32-byte header plus a 4-byte CRC. Header includes
codec IDs, block sequence, encoded/decoded lengths, four stream counts and base
timestamp. Predictor and compression dictionary state reset at every block.
Header and body are both covered by block CRC. All codec sizes include these
36 bytes; no shared dictionary or uncounted inter-block prediction is used.

Three allocation policies are evaluated:

1. **Page-aligned blocks, 14 data pages/sector.** Every block starts a fresh page.
   Page padding, page envelopes, complete sector allocations, sector headers and
   the reserved footer page are counted. This is the simplest recovery baseline.
2. **Stream-packed blocks, 14 pages.** Consecutive independent blocks share page
   payload space. A page header carries the first new block's byte offset, or
   0xFFFF if it only continues an earlier block. This supports resynchronisation
   after losing a page/sector without trusting raw magic bytes in sample data.
3. **Stream-packed blocks, 15 pages.** Reclaim the per-sector reserved footer page;
   append a session-end frame instead. A 256-byte clean-end allowance is included
   in the projection. A production END-frame encoder/command has not been added.

Policies 2/3 require a separate versioned container and are not changes to V2.0.
The data/header chunk recovery prototype exists and is tested, but is not a
production writer, complete session format or replacement for admission checks.

Payload utilisation before block padding:

```text
14 × 232 / 4096 = 79.296875%
15 × 232 / 4096 = 84.9609375%
```

Reclaiming page 15 improves theoretical capacity by **7.142857%**, not by a factor
of two. Stream packing separately recovers padding left at each block end. The
diagnostic sector remains excluded. Last-sector rounding and clean-end allowance
are included in exact 180/240/300-minute allocations in each results.json.

Capacity projections repeat the **measured block-size sequence** until storage
is full. They do not compress a repeated recording with a shared dictionary and
claim artificial savings. Each block is independently encoded. These are duration
estimates from short samples, not six-hour hardware endurance tests.

## Required compression

Frozen V2 produces 2880 record bytes/s at 100Hz IMU + 10Hz RMC + 1Hz GGA, before
EVENTs. Raw axis values alone are 1200 B/s and 21,600,000 bytes over 300 minutes.

| Target | Maximum physical recording B/s | Encoded B/s, 14 data pages | Reduction from 2880 B/s | Encoded B/s, 15 data pages |
|---|---:|---:|---:|---:|
| 180 min | 1553.067 | 1231.533 | 2.339:1 | 1319.476 |
| 240 min | 1164.800 | 923.650 | 3.118:1 | 989.607 |
| 300 min | 931.840 | 738.920 | 3.898:1 | 791.686 |

Encoded limits include block headers/CRCs but exclude page/sector overhead already
accounted in the budgets. The 15-page column subtracts the 256-byte clean-end
allowance. These limits assume no safety margin. For example, a 10% unused-space
reserve reduces the 300-minute 15-page budget to about **712.5 B/s**.

See the generated result overview for measured capacities. Two-second stream
packing improves the real-source estimate substantially over one-second aligned
blocks, but it does not establish reliable 300-minute recording. Only optimistic
stillness can cross 300 minutes with 15-page packing, without useful spare capacity.
The real-source 240-minute fit in that layout leaves only tens of KiB; do not
advertise it as a robust 240-minute guarantee.

No lossless codec can guarantee that arbitrary raw sensor values compress. Uniform
i16 input is included as an explicit counterexample. Raw fallback preserves data
and integrity but cannot guarantee a desired duration on an undersized chip.
No general-purpose codec trial here changes that limit.

## Recovery and correctness tests

Every measured predictor/backend block is decoded and compared to complete
canonical V2 record bytes before its size is accepted. Selected block sequences
are additionally encoded into both experimental page containers and recovered,
again with byte-for-byte comparison. Both sector layouts are tested.

Automated cases cover all movements, the full real source/tail, signed extremes,
u64 domains, duplicate timestamps/order, invalid fixes/missing UTC, random raw
data, raw fallback, bit/Rice/varint bounds, block byte flips, all truncation
positions, CRC-valid malformed lengths and independently decodable blocks.
Recovery tests tear a page at every byte position (including commit marker),
reject corrupt/unowned sectors, and recover earlier/later complete blocks.

Losing one page can invalidate an entire block and, with stream packing, adjacent
blocks sharing that page. The strict recovery policy also abandons the rest of a
damaged sector before resynchronising. Previously completed blocks survive, but
the worst interruption loss is not limited to a single sample or necessarily one
block. Two-second blocks trade better compression against a longer uncommitted
tail. Production admission/STOP/overflow policies must document that exposure.

## nRF52840 implementation recommendation

For this chip, the strongest practical candidate tested is **two-second bounded
blocks with adaptive per-column raw/delta/second-difference coding, exact timestamp
coding, escaped Rice/bit packing, and raw fallback**. LZSS is optional and not
justified by these results; omit it initially. Stream packing and a versioned
15-page sector profile offer additional capacity without losing precision.

No frozen V2 change is authorised by this experiment. A future compact profile
would use new magic/version IDs, keep V1/V2 decoders intact and preserve legacy
RECORD READ IMU-only CSV. It needs full session metadata, END-frame semantics,
staged flash commits, queue integration and independently checked host/firmware
decoders before hardware use. CRC locations/algorithm, append-only blank checks
and existing recordings must remain protected.

The Python implementation uses host containers and is not an embedded RAM
measurement. A static C++ port should budget roughly **20–32KiB additional working
RAM** for double buffering, bounded temporary columns and output/recovery buffers;
measure the actual implementation. The current Phase 2 build uses 80,456 bytes of
static RAM, leaving substantial but not unlimited space for this work and RTOS
stacks. Never replace acquisition buffers with an unbounded compressor heap.

Reported encode/decode times are CPython host measurements, not Cortex-M4 timing.
The adaptive encoder explores options per column; a C++ implementation can choose
from analytic bit costs rather than building every candidate. Limit work per main
loop, preserve the 10ms IMU deadline and measure worst-case CPU/queue occupancy.
The host supports Python integers beyond 64 bits for extreme u64 EVENT/timestamp
residual tests. A Cortex-M4 port must select raw/unsigned-delta mode before such
arithmetic would overflow; it must not assume native 128-bit integer support.

Full DEFLATE with a 32KiB window/memory level 8 needs roughly 260KiB of encoder
state by zlib's sizing formula, exceeding the board's available RAM alongside
firmware. A 512-byte window/memory level 1 is roughly 9KiB before other buffers,
but its modest measured saving does not establish 300 minutes. See the primary
[zlib memory-footprint documentation](https://www.zlib.net/zlib_tech.html).

Compression reduces programmed pages but adds CPU activity. No battery saving is
claimed without hardware current measurements. This benchmark does not measure
flash program timing, oscillator accuracy, MCU cycles, USB contention or electrical
power-fail behaviour.

**Recommendation:** use this codec family if extending the existing hardware's
typical duration is valuable, but do not promise 300 minutes at full precision.
Approximately 180 minutes is a more plausible development target for mixed motion,
still subject to representative data and margin; the noise/worst-case results
must also inform admission policy. A reliable minimum duration across activities
still requires more storage or an explicitly approved relaxation of requirements.
Collect substantially longer labelled, raw IMU + actual GPS + microsecond-timestamp
data before deciding the compact specification. Do not extrapolate the optimistic
stillness result into a sports-recording guarantee.

## Reproduction commands

Use the existing PlatformIO Python or another Python 3.10+:

```powershell
python tools/storage_benchmark.py --seconds 120 --block-seconds 1
python tools/storage_benchmark.py --seconds 120 --block-seconds 2
python tools/storage_benchmark.py --summarise
python tests/storage_benchmark/run_tests.py
python tests/recorder_v2/run_tests.py
```

Timings vary between hosts/runs. Canonical source hashes, encoded sizes, round-trip
results, exact page counts and capacities are deterministic with the stated seeds
and compressor implementation/version. No firmware build or serial access is
required. Existing physical recordings are neither read nor changed by these tools.
