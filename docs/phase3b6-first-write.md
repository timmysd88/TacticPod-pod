# Phase 3B.6: bounded five-second TCB3 test build

Prepared for review; **not uploaded and no hardware accessed** in this phase.
The board retains its disabled firmware and verified empty recording partition.
No physical program/erase/start or power interruption was performed.

## Decision and limits

**Conditional GO for one supervised, stable-power five-second engineering trial
using the dedicated artifact, after separate upload/write approval.** This is
not qualification for unattended, long-duration or power-failure operation.
If the unresolved physical SPI/provenance assumptions below are unacceptable,
the physical test remains NO-GO; passing simulation does not remove them.

The new `tcb3_first_write` PlatformIO environment inherits the board and Wire1
IMU flags and explicitly defines both TACTICPOD_TCB3_FLASH_ENABLED=1 and
TACTICPOD_TCB3_TEST_BUILD=1. The default environment still has TCB3 disabled.
No global enable was added to the source defaults or default environment.

| Guard | Enforced behavior |
|---|---|
| Capture duration | 5,000,000 monotonic microseconds from capture start |
| Finalisation deadline | 15,000,000 us total from capture start, including drain |
| Test write range | `0x000000–0x00FFFF`, exclusive end `0x010000` (64 KiB / 16 sectors) |
| Last permitted page start | `0x00FF00`; 256 bytes maximum, page aligned |
| Recording partition | Unchanged, `0x000000–0xFFEFFF` |
| Protected diagnostic sector | Unchanged, `0xFFF000–0xFFFFFF`; never targeted |
| Session allocation | Must recover append position zero; refuses existing occupied headers anywhere in the recording partition |
| Repeated start | Refused after one capture in a boot; occupied storage also refuses after reset |
| Other mutations | V1 RECORD START, RECORD ERASE/CONFIRM and destructive diagnostic test refused in the dedicated build |

The store engine enforces the write fence before page preparation/program entry
and before continuation allocation. The SPI adapter independently checks it
before WREN. Reading/scanning/exporting still uses the original recording
partition boundary, so this is a test allocation fence, not a partition change.
There is no erase method in the TCB store/adapter. V1 mutation APIs and the
diagnostic internal start entry also refuse in test mode, in addition to console
guards. Normal-build V1 recording and erase behavior are unchanged.

The capture deadline is checked before accepting observations and on each TCB
update, including updates where no foreground flash work is allowed. It stops
the compressor, seals retained input and drains without needing USB or a host
STOP. Records accepted by the test run are within the five-second window;
the stop notice/footer timestamp can occur just after the deadline on the next
foreground call. The first model run stopped at 5,000,088 us.

If draining has not completed by 15 seconds, the writer stops capture, aborts any
held-CS transaction and fault-locks. It does not retry, write a successful footer
or permit a new session. Releasing CS on an already-begun partial PP may initiate
programming of that partial page **inside the fence**; this is an incomplete-write
fault, not a successful full-page commit. No new page begins at/after the final
deadline. These are foreground firmware deadlines, not an independent hardware
watchdog: a CPU lockup or stalled low-level SPI call is not proven time-bounded.
Such a fault cannot cause the normal loop to keep scheduling more page writes.

## Prior evidence reviewed

* [Phase 3B.3](phase3b3-flash-safety.md): one full-page PP, inline CRC/seal in
  container 3.2, interruption models and paced throughput through storage full.
* [Phase 3B.4 hardware report](../logs/phase3b4-hardware/REPORT.md): repeated
  0B4018/0B17, SR1=00/SR2=02, matching SFDP and strong B-compatible fingerprint.
* [Phase 3B.5 review](phase3b5-clean-flash-plan.md) and
  [completed erase](../logs/phase3b5-erase/REPORT.md): 4095 sectors completed,
  16,773,120 bytes verified FF by firmware, no reported fault, append position
  zero, diagnostic sector excluded. No independent diagnostic-sector byte hash
  was available. Its preservation evidence remains address exclusion.

Frozen V2, V1 page serialization, TCB3 3.2 metadata/records/footer and TPX3 are
unchanged. Timed clean stop uses the existing clean footer reason; hardware-test
limits/build identity belong in the external test log and firmware hash, not a
silently changed binary schema. Legacy V1/V2 host decoding and RECORD READ CSV
remain supported; the dedicated test build intentionally refuses new V1 writes.

## SPI and fault audit

Successful programming remains one aligned `02h` transaction carrying exactly
256 data bytes under one CS assertion: WREN and WEL confirmation; four bounded
64-byte foreground transfer slices; CS high once; BUSY polling every 2 ms;
1-second program timeout; complete readback comparison and IEEE CRC32/seal
validation. Blank sectors/pages are verified before mutation. The seal is inline,
not a separate temporal commit or a claim of atomic programming.

The test identity preflight additionally requires SR1 exactly 00 and SR2 exactly
02 using read-only 05h/35h, as measured on this board. Any other response refuses
start/export; no protection/configuration repair is attempted. This is a narrow
measured-profile gate, not a generic B/F status decoder. It also rejects possible
suspend/protection flags before the first write.

The candidate B datasheet describes aligned 256-byte ordinary Page Program with
WREN/WEL/WIP handling; tPP maximum is 0.75 ms under its stated conditions. It
specifies CS setup/hold minima of 5 ns and CS-high minimum of 20 ns. It does not
specify a maximum gap between page data bytes in the reviewed timing table.
Using mode 0 at 1 MHz with idle-low SCLK during held-CS foreground gaps is a
reasonable protocol inference for this bounded trial, **not an exact-die vendor
guarantee**. [Manufacturer-authored XT25F128B Rev 1.2, sections 6.14 and 7.8](https://datasheet.lcsc.com/datasheet/pdf/8d867a672fb33d557ab1ba9942d04fab.pdf?productCode=C558846).

The installed Adafruit core uses software CS; SPI begin/end transactions enable/
disable SPIM and do not bracket the whole operation in an IRQ mask. Interrupts
can run between slices. IMU uses Wire1 and GPS uses UART, not this SPI bus. While
TCB owns the flash, automatic DIAG, legacy writer operations and destructive
commands cannot interleave with the held-CS page. Export starts only when idle.
GPIO/core execution overhead is unmeasured: wire time alone does not establish
actual maximum service time or acquisition jitter. A passive logic-analyzer
capture during the later approved trial is recommended.

Fault paths retained: unexpected status/WEL, program timeout, nonblank target,
readback/CRC mismatch, full test fence, acquisition deadline loss and compressor
overflow stop capture or lock writes. No fallback overwrites old data or erases
space. The finalisation watchdog adds a fault for a retained/stalled drain. It
preserves previously verified pages; it does not claim to salvage a partial
frame. On a write fault, read-only export may require a later reviewed reboot
because the flash ownership lock deliberately remains engaged.

## Simulated validation

The new first-write suite executes production ARM main-loop, parser, compressor,
session, store and SPI-adapter code against RAM NOR, with USB disconnected.
It verifies:

* Five-second autonomous capture, no manual STOP, byte-exact original typed
  observations through persisted pages and TPX3 export/host decoding.
* Expected SR2 rejection, OBS-active rejection, existing occupied sectors at and
  beyond the test fence, and repeated START refusal.
* Capture cutoff through TIMER4 low-word rollover.
* Finalisation timeout even with allowWork=false; held-CS timeout releases the
  bus, reports incomplete writes and never retries.
* Explicit missed-acquisition stop and deadline footer reason.
* Independent adapter refusal before WREN for out-of-fence/unaligned addresses.
* Core engine filled through the last test sector using maximum raw frames,
  reports full and writes its footer within the fence; all later bytes unchanged.
* Actual USB command parser/internal diagnostic entry refusal of V1 writes and
  all erases; a 20-second disconnected boot never starts recording or WREN.

The first high-entropy, 10 ms simulated PP-busy run captured **501 IMU, 50 RMC,
5 GGA** (14,424 exact reconstructed bytes), used three sectors / 12,288 allocated
bytes, issued 39 page programs and generated a 13,504-byte TPX3 export. Output
queue high-water was one; max modeled SPI service was 544 us. The rollover run
captured 500/50/5. Endpoint phase accounts for the one-IMU difference. These are
simulated samples/timing, not physical measurements or a promise of exactly 500.

Existing whole-capacity, 120-second concurrent, corruption, interrupted-page,
overflow, timeout, mixed V1/V2/TCB3 and export suites remain required. Exact
commands/pass results are saved in `logs/phase3b6-review/regressions.json`.
No test opens a serial device or touches physical flash. The test harness's
16 MiB memory and large expected-wire arrays do not belong to firmware RAM.

## Build and artifact review

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e tcb3_first_write
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" tests/tcb3/first_write_tests.py
```

The explicit test ZIP is `.pio/build/tcb3_first_write/firmware.zip`; the default
ZIP remains in `.pio/build/seeed_xiao_nrf52840_sense/`. Never substitute an old
unbounded enabled sizing build. Build logs, size and artifact hashes are saved
with this review. RAM figures are static allocation; full-system live stack/heap
high-water and physical execution bounds remain unqualified.

| Final build | Static RAM | Program flash |
|---|---:|---:|
| Default, TCB disabled | 118,992 B / 237,568 B | 165,336 B / 811,008 B |
| Dedicated first-write | 120,692 B / 237,568 B | 181,392 B / 811,008 B |

Both builds pass. The default ELF and application payload are byte-identical to
the currently uploaded disabled firmware. The first-write ZIP SHA256 is
`a8d58032d620e320adfed80be8462aee2b646cf6b4597c2c93182c0f5851ae44`;
ELF SHA256 is `eae2df98fbc2418a185941db2389a7fee6ca3395befa2c9f4eb1d3fb9ae085d4`.
All **17 regression suites pass**, including **11 new bounded-build scenarios**.
The simulated TPX3 example was also decoded through the standalone CLI and its
14,424 reconstructed bytes compared exactly with the captured source records.
Evidence/artifact hashes and complete commands are under
`logs/phase3b6-review/`; example files there are simulated, not physical recordings.

Files changed: `platformio.ini`, `src/tcb3_test_limits.h`, `src/tcb3_store.h/.cpp`,
`src/tcb3_session.h/.cpp`, `src/recorder.cpp`, `src/flash_write_test.cpp`, and
`src/ram_observations.cpp` (inactive-in-firmware source-comparison hook only).
Test additions are `tests/tcb3/first_write.cpp`, `first_store.cpp`,
`first_console.cpp`, `first_write_tests.py`, with test-only SPI model support in
`tests/recording/SPI.h`. No host decoder or frozen-format change was needed.

## Proposed later physical trial: exact commands

No commands below were executed on hardware during this phase. After separate
approval, upload only the reviewed dedicated ZIP by the proven nrfutil procedure.
Then wait for read-only recovery scan and automatic GPS profile verification.

Preflight, one command/report at a time:

```text
IMU OFF
RECORD STATUS
OBS STOP
COMP STOP
OBS STATUS
COMP STATUS
TCB STATUS
GPS STATUS
DIAG ID
```

Require V1 stopped/append zero, OBS off/fast off, COMP off/fault zero/queues empty,
test-limit marker `test=5s,64KiB,15s`, GPS 10 Hz RAM verified, expected identity/
status/SFDP. A nonempty partition, unexpected status, active GPS diagnostic or
retained compressor RAM is a refusal condition, not a reason to erase/unlock.

Send **TCB START once**. Wait for STARTED after its read-only scan. Capture
status/COMP timing reports sparingly during the five-second window, then let
firmware auto-stop. **TCB STOP** remains an optional early-stop command; do not
start OBS or COMP separately. Wait for TCB STOPPED and the legacy read-only
rescan. At 15 seconds without clean completion, expect a fault and preserve
evidence; do not send another START. A host-side timeout is not proof of success.

When idle, **TCB STATUS** reports state/counters and **TCB READ** produces the
read-only binary TPX3 stream. Capture raw bytes through the validated END packet
without newline/text conversion or interleaved commands. Repeat READ and compare
hashes. The existing decoder is a local-file decoder, not a serial download
client. A controlled binary-safe host runner must save that stream during the
later authorized test.

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" tools/tcb3_decode.py first-write.tpx3 --raw-output first-write.v2wire > first-write.json
```

Require empty export/root/session issues, clean final footer, offered=committed
per type, no loss/reject/missed/full/CRC/incomplete errors, monotonic uint64 device
timestamps and approximately 100/10/1 Hz within the measured five-second window.
Validate UTC advancement/fix masks independently; absent/invalid GPS fields must
remain absent/invalid. Both valid and invalid observations are legitimate stored
records. CLI exit zero alone is insufficient if issue lists are populated.

Stop on unexpected reset, memory error, SPI fault, sample loss or queue overflow.
Keep stable power; do not experiment with interruption. If fault-locked, preserve
logs and already committed blocks, wait for any flash operation to settle and
review a read-only recovery reboot/export. No automatic reset, retry, erase or
second recording is planned. After a successful run, further START is refused;
the artifact is deliberately single-session, not production firmware. Leave
recorders stopped and release USB. The diagnostic sector is never a test target.

Remaining physical risks: exact chip provenance, electrical CS/SCLK behavior,
actual SPI/CPU/IRQ timing and RAM high-water, and brownout effects outside the
CRC recovery model. The review-limited trial measures the first of the write
timing behaviors; it does not resolve every qualification risk.
