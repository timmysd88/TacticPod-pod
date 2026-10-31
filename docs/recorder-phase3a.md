# Phase 3A: lossless compression in RAM

Implemented without a compact flash writer or physical device access. The
[compact 3.0 wire contract](recorder-compact-v3.md) is separate from frozen V2.0.

## Files delivered

| Change | Files |
|---|---|
| New embedded encoder | src/compact_recorder.h, src/compact_recorder.cpp |
| Foreground observation/scheduling/command integration | src/ram_observations.cpp, src/main.cpp, src/serial_console.cpp |
| New host decoder | tools/compact_decode.py |
| New format and validation documents | docs/recorder-compact-v3.md, docs/recorder-phase3a.md |
| New ARM/host/resource/timing tests | tests/compact/harness.cpp, run_tests.py, test_decoder.py, test_resources.py, test_timing_model.py |
| New exact-byte fixture and hashes | tests/compact/fixtures/stillness.tcb3, stillness.v2wire, manifest.json |
| Existing regression integration | tests/gps/harness.cpp, tests/gps/observation_cases.h, tests/gps/run_tests.py, tests/recording/harness.cpp, tests/flash_write/harness.cpp |

## Hardware-validation gate

Before implementation, the Phase 2 validation record in
[recorder-v2-ram.md](recorder-v2-ram.md) was checked: it explicitly requires
hardware timing/resource/power validation after a separately instructed upload.
There is no recorded completion of that gate. **Phase 2 hardware timing remains
outstanding.** This RAM-only phase does not waive it or claim hardware approval.
No firmware was uploaded, serial port opened, flash read, programmed or erased.

## Integration and controls

Production sources: src/compact_recorder.h and src/compact_recorder.cpp.
src/ram_observations.cpp copies each fresh typed observation into the compressor
before its independent OBS queue admission. src/main.cpp runs bounded compression
after acquisition/V1 enqueue, or while at least 1ms of scheduled slack remains.
src/serial_console.cpp registers these exact commands:

| Command | Effect |
|---|---|
| COMP START | Explicitly enable RAM capture; also request 100Hz IMU while active |
| COMP STOP | Stop this capture, seal its partial block and continue draining compression work |
| COMP STATUS | Bounded deferred report: capture/fault, accepted/rejected observations, sealed/completed blocks, source/output occupancy, backpressure, fallback and timing |
| COMP CLEAR | Explicitly discard retained compression RAM while stopped; no change to clock, OBS, V1 or GPS settings |

COMP is disabled at boot. START refuses retained sources/output; CLEAR refuses
while capturing. Controls/reporting are suppressed/refused during V1 CSV export.
USB disconnection does not stop capture. OBS STOP does not stop COMP capture;
COMP STOP does not stop V1 recording. Invalid/missing-UTC GPS observations and
leap-second EVENT records retain the original wire bytes. No stale GPS cache is
read by the compressor.

There is deliberately no automatic output consumer. The output()/release() API
offers immutable oldest-first RAM blocks for a future consumer. A sustained
COMP run without one fills both outputs, then both sources, and latches an
explicit fault on the first rejected observation. Nothing silently recycles an
unconsumed block. V1 continues to acquire/write normally. Thus COMP START is a
bounded RAM diagnostic, not yet a continuous compressed recording command.

## Fixed RAM and processing

The normal PlatformIO build succeeds with static RAM **118,440 / 237,568 bytes**
(49.9%) and program flash **160,804 / 811,008 bytes** (19.8%). Phase 2 used
80,456 bytes: increase **37,984 bytes** including alignment/link effects.

| Compressor object | Allocated bytes |
|---|---:|
| Two source slots, including metadata | 16,448 |
| Two output slots, including metadata | 16,480 |
| Shared indices/column/cost/state workspace | 4,560 |
| Reporting/statistics/control globals | 478 |
| Total writable compressor objects | 37,966 |

No heap allocation, recursion, data-dependent allocation or dictionary exists.
Source payload capacity is 8192 bytes/256 observations per slot. Each output
holds at most 8228 bytes, including the raw fallback envelope/CRC. EVENT u64
details use raw columns to avoid overflowing signed predictor arithmetic.

ARM stack-write instrumentation measured **192 bytes** for encoder calls and
**456 bytes** for command calls. Static plus the larger measured stack is
**118,896 bytes**. This is an observed isolated-path RAM peak, not whole-device
peak certification: live ISR nesting, framework/RTOS stacks and runtime heap
high-water require hardware measurement. Budget at least another 1KiB for
compression call-stack headroom and validate the existing system's full margin.

Each update executes at most eight bounded work units, checking the shared
microsecond clock between them against a 200us target. A unit indexes/gathers/
emits one observation or column value, selects among seven modes with 17 Rice
parameters, copies 16 raw bytes, or CRCs 16 bytes. No entire block is encoded in
one main-loop call. The last unit may overrun the target, so 200us is not a
hard upper bound. Diagnostic output is independently gated on USB TX space.

Every benchmark block is profiled using actual Cortex-M4 Thumb encoder machine
code in Unicorn, matching the framework's -Ofast optimisation. The largest
tested arithmetic-stress block needs approximately **2.12 million instructions**;
the largest tested update/harness slice is approximately **7,000 instructions**.
This includes output-copy/test overhead and excludes real interrupt/wait costs.
Instruction counts are not MCU cycles or measured execution time.

For ordinary two-second mixed-rate blocks, explicit instruction-cost sensitivity
tests at 1/4/12 assumed cycles per instruction model about **28/113/345ms** total
work, respectively; maximum modeled compression slices are **95/241/330us**.
The timing gate is exercised by advancing the mocked microsecond clock from
the modeled instruction cost. These assumptions are illustrative, not a proven
WCET bound. Runtime diagnostics expose measured maximum slice and per-block
step time for later hardware validation; block step time excludes foreground
guard/reporting overhead and idle intervals. A trustworthy physical worst-case
time per block remains outstanding and cannot be established without the
hardware access prohibited for this phase.

## Capacity projection on the existing 16MiB chip

No compressed data is yet written to flash. Projection assumes the retained
4095 recording sectors, 14 data pages/sector, 232 payload bytes/page, with each
complete compact block starting on a fresh data page. Blocks may continue
across sector boundaries. This leaves **57,330 data pages / 13,300,560 payload
bytes**, excludes the diagnostic sector, and rounds each frame up to whole
pages. It assumes one continuous session and full otherwise available storage;
many short sessions, existing occupied sectors, future metadata/framing, safety
margin and power interruptions reduce capacity.

| Source | Mean frame bytes | Projected minutes at 100Hz IMU |
|---|---:|---:|
| Simulated stillness | 1533.67 | 273.00 |
| Simulated walking | 1937.37 | 212.33 |
| Simulated sprinting | 2358.05 | 173.70 |
| Simulated jumping | 2018.28 | 212.33 |
| Simulated rapid turns | 2072.85 | 211.13 |
| Simulated noisy motion | 2536.52 | 172.67 |
| Adversarial uniform raw axes | 3096.55 | 136.50 |
| Existing real IMU recording + synthetic GPS | 1845.88 | 210.76 |

The real source is the existing logs/device-monitor-261027-204904.log V1 CSV:
3000 samples from its 3026 reconstructable raw samples enter the capacity run,
and the final 26 are tested separately as a partial block, with
their original millisecond-derived timestamps retained. GPS is synthetic,
not claimed as real movement/navigation evidence. Reblocking uses actual time,
not a presumed 200 samples per two seconds. Capacity normalises by actual IMU
count at 100Hz; the partial final block is not counted as two extra seconds.
Other sources cover 120 seconds each at 100Hz/10Hz/1Hz, including timestamp
jitter, invalid fixes, missing fields and noisy GPS integers.

300 minutes needs 9000 two-second blocks in those 57,330 pages: mean <=6.37
pages/block, or <=1477.84 bytes/block before page rounding. Even the optimistic
stillness mean exceeds that allowance. **Do not promise 300 minutes on this
chip with this lossless format.** True raw fallback has no compression guarantee:
a nominal 5760-byte canonical block plus 36 bytes consumes 25 pages, about
76.43 minutes in this geometry. The much better benchmark results depend on
sensor statistics, not discarded precision. There is no 300-minute admission
guarantee for arbitrary six-axis raw data on 16MiB.

## Verification and next gate

Final build and all nine suites pass. There are 298 named regression/host tests,
456 native ARM input/control cases (including all 436 benchmark blocks),
21 explicit timing-sensitivity runs and one resource check. Additional each-byte
corruption/truncation assertions run inside those suites. SHA-256 comparison
confirms the V1 recorder/writer/format, flash diagnostics, GPS config/profile/rate
code, platformio.ini and frozen V2 specification are unchanged. The final status
is retained in .pio/phase3a-regression-results.json.

The existing regression suites remain in use: GPS/clock/observations plus four
new COMP main-loop cases (194), V1 recording (20), flash diagnostics (13), frozen
V1/V2 host format (32), storage benchmark (26). Compact validation adds 13 host
decoder tests, 10 ARM control/backpressure scenarios, five V2-bearing golden
inputs, 436 benchmark blocks after time-based real-data reblocking, two explicit
raw-fallback cases, two full-size/extreme-precision cases, the real 26-sample
partial tail, 21 timing-sensitivity
runs and a resource check. Every native block is decoded with the host inverse
and compared against its complete canonical input bytes.

Main-loop simulations exercise 15 seconds of concurrent 100Hz IMU, 10Hz RMC and
1Hz GGA, including OBS stopped and output backpressure. All 1500 V1 samples
commit with no simulated losses/missed deadlines; ordinary RAM-only operation
does not issue any flash writes. Decoder tests cover all column and timestamp
modes, each-byte CRC corruption/interruption, valid-CRC malformed headers,
counts, order, widths/modes, sequences and 1000 deterministic CRC-valid mutations.
Later intact blocks are independently decodable; the strict stream API fails
explicitly on an interrupted frame. Existing flash tests simulate page tears;
no compact physical commit/recovery implementation is implied.

Run from the project root with the existing PlatformIO Python/toolchain:
The resource check consumes the normal firmware ELF, so build first.

```powershell
platformio run
python tests/gps/run_tests.py
python tests/recording/run_tests.py
python tests/flash_write/run_tests.py
python tests/recorder_v2/run_tests.py
python tests/storage_benchmark/run_tests.py
python tests/compact/run_tests.py
python tests/compact/test_decoder.py
python tests/compact/test_timing_model.py
python tests/compact/test_resources.py
```

Deterministic size/capacity and instruction results are saved in
.pio/phase3a-native-results.json; resources in .pio/phase3a-resources.json;
timing sensitivity in .pio/phase3a-timing-model.json. Logs and link artifacts are
local build products. The checked compact fixtures/manifest remain under
tests/compact/fixtures, independently usable without Unicorn.

Next, complete separately authorised Phase 2 hardware timing/resource validation,
then measure compression cycles, acquisition jitter, queue occupancy, worst-case
interrupt/USB contention and current consumption on the real MCU. Reconfirm
GPS startup settings and STOPPED V1 state. Before any compact physical writes,
review a new outer session/page format, metadata, partial-block discard/recovery,
V1/V2 compatibility, storage admission and power-cut tests. Preserve the current
partition and 14-page geometry for that first writer. No firmware upload, physical
flash writing or purchase is authorised or assumed by this report.
