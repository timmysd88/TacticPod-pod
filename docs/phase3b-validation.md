# Phase 3B.1 / 3B.2 simulated validation

2026-10-31. Persistent TCB3 sessions and TPX3 export are implemented and tested
using host NOR only. **NO-GO for first physical TCB3 recording.** No firmware
upload, serial connection, physical flash read/write/erase or destructive
diagnostic was performed for this phase. No Phase 3B recorder runs at boot.

## Implementation delivered

| Files | Change |
|---|---|
| src/tcb3_store.h, src/tcb3_store.cpp | Portable append-only store, sliced body/commit verification, metadata/footer accounting and read-only export packet state machine |
| src/tcb3_session.h, src/tcb3_session.cpp | Disabled-by-default activation gate; SPI/USB adapter, ownership, compressor leases, stop/drain/failure controls and counters |
| src/main.cpp | Service after acquisition or with >2ms scheduled slack; TCB missed-interval notification; diagnostic arbitration |
| src/ram_observations.cpp | Count offered typed observations before compressor admission; no parser/scaling changes |
| src/serial_console.cpp | Separate TCB controls and ownership/binary-export guard |
| src/recorder.cpp | Add TCB export to the existing text-suppression predicate; V1 flash writer, allocation and CSV byte format otherwise unchanged |
| src/flash_write_test.cpp | Keep the actual USB command reader running while TCB owns flash; intercept conflicting confirmations and suppress reconnect text; diagnostic program/erase logic unchanged |
| tools/tcb3_decode.py | Strict TPX3 parser, persistent frame recovery, V1/V2 mixed-image delegation and JSON-safe uint64 output |
| tools/compact_decode.py | Add --container dispatch; existing standalone frame API remains compatible |
| tests/tcb3/* | Actual ARM engine/model and enabled main-loop tests, deterministic export/expected-wire/JSON fixtures |
| Existing tests/recording/SPI.h and tests/flash_write/Arduino.h, LSM6DS3.h, harness.cpp | Optional SPI-time/noise/sink hooks, binary-safe USB mock writes, configurable simulated program delay and a real-console harness selection; default regressions retain existing behaviour |
| docs/recorder-tcb3-persistence.md | Exact separately versioned outer page/container/export specification and address safety map |

The frozen docs/recorder-v2-format.md, src/recorder_format.h, GPS receiver/config/
profile sources and platformio.ini are unchanged. TCB3 frame 3.0 is unchanged;
the new persistent outer format is **3.1.0**, with TPH3/TPD3/TPE3 page magic.
No V1/V2 recording is converted in place. RECORD READ remains the original
V1 IMU-only CSV; TCB READ is separately framed binary. Existing mixed V1/V2
images are compared byte-for-byte before and after simulated TCB append.

## Address and erase/write safety map

| Addresses | Use | New writer behaviour |
|---|---|---|
| 000000..FFEFFF | 4095 x 4KiB recording sectors | Read-only scan; append only into fully verified blank sector/page |
| Each sector page 0 | Session/continuation header | Repeat 160-byte metadata; CRC and last commit |
| Each sector pages 1..14 | Fragment data | 216 exact TCB3 bytes/page; frame may cross sectors |
| Each sector page 15 | Optional final footer | Clean/loss/full/deadline result; no footer written after uncertain I/O fault |
| FFF000..FFFFFF | Existing diagnostic sector | Excluded from new writer and exporter, including reads |

TCB has no erase API or command. The simulation model has an erase primitive
only to test erased-bit and sector-alignment rules; actual store/export paths
never call it. A nonblank prefix, including V1/V2/TCB3/unknown/torn headers,
reserves its sector. Hidden payload under a blank header causes refusal.
Write helpers enforce exclusive end FFF000, <=64-byte operations and page bounds.
The SPI adapter never changes protection registers or attempts recovery writes
after a timeout/readback/identity/status fault. A diagnostic-sector canary is
verified unchanged. Full handling writes only the already reserved final footer.

## RAM, queue bounds and processing

| Build / object | Bytes |
|---|---:|
| Normal build static RAM, physical feature disabled | 118440 / 237568 |
| Normal program flash | 161060 / 811008 |
| Explicitly enabled sizing build static RAM | 120076 / 237568 |
| Enabled program flash | 176976 / 811008 |
| Additional enabled static RAM over Phase 3A | 1636 |
| Store instance | 712 |
| Binary exporter instance | 580 |
| Deferred status line | 256 |

The remaining increase is fixed control/transport/counter state and alignment.
There is no new full-frame copy, heap allocation, recursion or unbounded queue.
The two existing 8228-byte output slots are the flash queue; one remains leased
until the frame's last page commit verifies. Maximum queued frame storage is
16456 bytes, already allocated by Phase 3A. Two existing 8192-byte input slots
provide another 16384 bytes; source metadata/indices/cost workspace are unchanged.
Pressure tests reach both input slots and both output slots, detect one rejected
record and stop capture. Retained accepted data drains and the rejected record
is explicitly represented in the footer loss count.

Stack-write instrumentation of complete ARM integration paths observes at most
888 bytes, including the test caller and mocked drivers. Enabled static plus
that observed stack is 120964 bytes. This excludes hardware interrupt nesting,
real framework/RTOS stack peaks and runtime heap high-water. It is not a
whole-device peak RAM certification. Compiler .su files additionally expose the
individual store/session function stack sizes.

The store performs at most one <=64-byte transport operation per service call;
programming includes status/WREN/WEL checks. At 1MHz the modeled largest SPI
service is **600us**. The main-loop model adds six raw-read costs totalling
180us/sample and a 100us loop cost. Compression remains sliced by the existing
eight-unit/200us-target rules; no WIP busy wait was introduced.

Normal and high-entropy 15-second main-loop tests both commit 1500 IMU, 150 RMC
and 15 GGA observations. Each original individual timestamp, axis, validity,
UTC field and cross-stream order is compared byte-for-byte with the decoded
stored image. Constant-input IMU spacing is 9912..10088us; high-entropy spacing
is 9908..10092us. No >=20ms acquisition gaps or missed-interval events occur in
those models. Source high-water is 2/2, output high-water 1/2 in these cases.

Four maximum raw 8228-byte frames, each 39 pages, take 1820/1920/1920/1920ms to
commit in the separate 1ms-service/3ms-program storage model. This is a stress
bound for that model, not MCU WCET or a claim that the ordinary 100Hz/10Hz/1Hz
stream generates 8192 canonical bytes every block. It deliberately tests the
largest admitted frame, including raw fallback. It excludes the encoder's CPU
cost; do not add optimistic independent throughput figures to claim a proven
physical 2-second deadline.

At 100ms simulated program latency, high-entropy nominal-rate capture reaches
the bounded queue limit after 803 IMU/81 RMC/9 GGA offered observations. It safely
stops, drains 802 IMU/81 RMC/9 GGA and records exactly one rejected/lost IMU.
No erase, overwrite or diagnostic address access occurs. An injected missed
interval separately produces reason 3 with missed=1 and no fabricated samples.
Input may therefore stop safely under adverse flash timing; continuous lossless
throughput is not guaranteed for arbitrary program latency.

## Recovery, corruption and compatibility evidence

All **12 suites pass**, including all nine existing firmware/host suites,
31 new storage/decoder cases, seven enabled main-loop scenarios and one real
USB command-parser scenario. The existing compressor matrix still checks all
436 source blocks byte-for-byte. Results are recorded in
.pio/phase3b-regressions.json and .pio/phase3b-validation-summary.json.

The new store suite covers erased-bit/page/sector model rules; no boot writes;
normal/empty-close metadata and continuation; exact persistence and export;
mixed V1/V2 preservation; reset append; torn/unknown headers; hidden payload;
flash full before diagnostic space; CRC/readback failure; rejected programming;
boot/program timeout; torn commit; full uint64 microsecond timestamps across
2^32; millisecond poll rollover; corrupt fragment recovery to later independent
frames; malformed CRC-valid fragment/footer fields; export corruption/truncation;
and an unchanged diagnostic-sector canary.

One actual production write trace is replayed at **2650 byte-prefix power-cut
positions over 50 program operations**. Only complete committed blocks may be
reconstructed. An interrupted footer cannot invalidate earlier complete blocks;
an interrupted frame is discarded as a whole. Power-fail loss counts without a
trusted footer remain unknown. This does not simulate analog brownout behaviour.

Seven enabled production main-loop scenarios cover normal acquisition, high
entropy, deliberate slow-flash overflow, injected missed deadlines, export
backpressure/disconnect/command isolation and V1 -> TCB -> V1 -> TCB ownership
and append-cache refresh, plus fully saturated OBS retention queues. That final
case still commits all 1500/150/15 TCB observations with zero recording loss;
OBS diagnostic overflow must not be confused with TCB/COMP rejects. The USB test sends packets in 31-byte writes after
zero-space backpressure, checks binary zeros are preserved, and verifies exact
host reconstruction. No program commands occur during export. Old RECORD
commands and CSV regressions run separately without enabling the feature gate.
A further enabled test exercises the actual USB line parser, not a direct API
shortcut: STATUS and STOP remain usable while TCB owns flash, and competing
recording, RAM-clear and diagnostic confirmation commands are refused. Export
restart is tested after a partial 31-byte packet prefix.
STOP during the pending scan is also tested: it cannot lead to a delayed automatic
start, and the incomplete scan cannot authorise any allocation.

## Binary export example

tests/tcb3/fixtures/example.tpx3 is 9024 bytes. It decodes to one complete
three-block session containing 600 IMU, 60 RMC and 6 GGA observations, exactly
17280 reconstructed V2 wire bytes. Expected decoded JSON and wire bytes are
checked alongside SHA-256 hashes in manifest.json. The example uses existing
deterministic benchmark data, not new hardware recordings.

```powershell
python tools/tcb3_decode.py tests/tcb3/fixtures/example.tpx3
python tools/compact_decode.py --container tests/tcb3/fixtures/example.tpx3
```

Packet/flash decoding errors remain explicit. TCB-only exports are not full
flash backups; unexported V1/V2 sectors are not reported as physically erased.
JavaScript consumers must retain uint64 precision with BigInt/decimal strings.

## Reproduction and decision

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
python tests/tcb3/run_tests.py
python tests/tcb3/integration_tests.py
python tests/tcb3/console_tests.py
```

Results/logs are retained as .pio/phase3b-*. The normal and enabled sizing builds
both succeed. Checked fixtures are verified without being rewritten during normal
tests; regeneration requires the explicit TCB3_UPDATE_FIXTURES=1 environment setting.
All existing and new suites must pass before changing the gate.

**GO for software review and further simulated validation. NO-GO for physical
TCB3 recording in this delivery.** The default gate stays disabled. Before a
first physical recording, obtain separate explicit approval and resolve:

1. Exact 0B4018 part identification, partial-page-program/retention limits and
   program latency. This adapter uses five program operations per page, including
   its marker; JEDEC/SFDP capacity does not establish that this count is allowed.
   A model with a four-program limit safely refuses the fifth, but only the exact
   part's documentation/hardware can qualify the real behaviour. Also verify full pipeline throughput under
   compression, SPI/I2C, interrupts, UART and USB contention. No physical deadline
   guarantee follows from these models or the earlier RAM-only short capture.
2. Live stack/heap high-water and extended TIMER4 rollover operation. Active BLE
   coexistence remains untested if BLE is later enabled.
3. Hardware failure/power-cut behaviour with a backed-up flash and an explicitly
   bounded blank test area, without sacrificing existing recordings.
4. Build identity provisioning (currently explicitly unavailable) and client
   handling of incomplete sessions, unknown loss counts and interrupted exports.

These are documented limitations, not assumptions that hardware was validated.
No enablement, upload, physical writing or erasure is part of Phase 3B simulated
validation. The preserved frozen V2 contract and existing V1 data remain intact.
