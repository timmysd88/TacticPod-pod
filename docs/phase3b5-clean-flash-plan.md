# Phase 3B.5: proposed clean-flash and first-recording procedure

2026-10-31. **Review only: no erase, upload, TCB3 enable or recording was
performed.** Permanent deletion is authorized in principle, but the explicit
instruction to wait for review governs execution. This document is not an
automatic execution script.

Decision: **conditional GO for the recording-partition sector-erase procedure;
NO-GO for an immediate physical TCB3 recording.** Erasure and first recording
are separate gates. The latter still requires approval of the write qualification
risks and an explicitly enabled, tested build. Deleting old sessions does not
resolve physical programming timing, interrupted-program behavior or chip
provenance.

## Evidence and current state

Phase 3B.4 measured ten identical JEDEC `0B4018` reads, legacy ID `0B17`,
SR1=`00`, SR2=`02`, and four identical SFDP images. The BFPT at `0x30` identifies
128 Mbit / **16 MiB**, three-byte array addressing, and a 4 KiB erase descriptor
with opcode `20h`. The parameter layout and vendor table support an XT25F128B-
compatible fingerprint; they do not authenticate exact die/revision or resolve
the W25Q128 breakout marking. See the [complete hardware evidence](../logs/phase3b4-hardware/REPORT.md).

Fresh status-only USB queries during this review confirmed:

* RECORD STOPPED, buffered=0, dropped=0, missed=0, next_sector=`0x10000`.
* OBS capture=OFF, RAM_fast_imu=OFF. Its retained boot-observer queues/overflow
  counters are historical and unchanged: IMU=228, RMC=453, GGA=41.
* COMP capturing=0, fault=0, input/output empty.
* TCB DISABLED. COM8 was closed afterward.

Only RECORD STATUS, OBS STATUS, COMP STATUS and TCB STATUS were sent today.
No stop/start/clear, flash configuration, erase or program command was sent.
Repeat the preflight after any reset/reconnect before future execution; OBS
defaults to passive capture after a firmware reset.

## Erase implementation review

The existing `Recorder::handleCommand`/`update`/`serviceIO` implementation in
`src/recorder.cpp` requires an idle recorder and diagnostic engine. Console guards
also exclude TCB ownership and GPS diagnostics. RECORD ERASE only arms a warning;
RECORD ERASE CONFIRM must arrive within 30 seconds. Confirmation consumes the arm,
rechecks JEDEC five times, and starts at address zero.

Each sector: read SR1; reject FF/BUSY; issue WREN; require WEL set and WIP clear;
issue `20h` plus a three-byte aligned address; raise CS; poll `05h` at 2 ms
intervals. WIP still set after 5 seconds, FF status, or WEL remaining set after
completion faults and locks flash operations until reboot. The implementation
does not unlock protection or write status registers. It does not inspect SR2
during erasure: preflight status review therefore matters.

After WIP clears, it reads **all 4096 sector bytes**, in sixteen 256-byte reads,
and requires every byte FF before advancing. A mismatch faults; there is no
silent retry. On the final verified sector it resets append position to zero,
returns to idle and prints:

```text
RECORD erase complete; diagnostic sector untouched. Recording remains OFF.
```

This is compatible with the measured descriptor. The candidate B datasheet
specifies WREN, three address bytes, WIP polling and WEL reset for Sector Erase;
tSE is 80 ms typical / 800 ms maximum under its stated conditions. Its ordinary
page-program maximum is 0.75 ms. These candidate limits are not physical timing
measurements. [XTX XT25F128B Rev 1.2, sections 6.16 and 7.8](https://datasheet.lcsc.com/datasheet/pdf/8d867a672fb33d557ab1ba9942d04fab.pdf?productCode=C558846).

The erase helper checks BUSY/unavailable status and WEL, not every protection
bit. Stable initial SR1=00/SR2=02 is required for this proposed test, with no
attempt to repair unexpected protection/configuration. WIP=0/WEL=0 does not by
itself qualify an unknown part. A device silently rejecting an erase is caught
by WEL/readback unless that sector was already blank, in which case the required
blank outcome already holds.

## Exact mutation boundaries

| Operation | Permitted addresses |
|---|---|
| Recording partition | `0x000000–0xFFEFFF`, exclusive end `0xFFF000`; 16,773,120 bytes |
| Future erases | Exactly 4095 operations at `n * 0x1000`, n=0..4094 |
| First erase | `20 00 00 00`, affects `0x000000–0x000FFF` |
| Last erase | `20 FF E0 00`, affects `0xFFE000–0xFFEFFF` |
| Diagnostic sector | `0xFFF000–0xFFFFFF`; excluded from erase, TCB writes and TPX3 export |
| Future TCB page starts | Aligned to 256 bytes, at most `0xFFEF00`; never crossing `0xFFF000` |
| Sector page 0 | Header/continuation, one 256-byte program |
| Sector pages 1–14 | Fragment pages, one 256-byte program per used page |
| Sector page 15 | Optional stop footer, one 256-byte program |

No 32/64 KiB or chip-erase operation is used. No diagnostic erase/test command
is part of this procedure. The normal V1 erase path sends WREN and Sector Erase,
**no Page Program**. It does not write session headers or resume acquisition.

The protected diagnostic sector is excluded by software boundaries; SR1/SR2 do
not establish that it has separate hardware write protection. A complete before/
after hash of that sector is not exposed by existing serial commands. DIAG and
DIAG ID identify the chip; they do not dump diagnostic-sector contents. If a
physical byte-level preservation certificate is required, prepare a separately
reviewed read-only dump before erasure. Do not substitute the destructive test.

## Proposed erase execution, after review

1. Use the currently uploaded normal build, with TCB disabled. Keep stable USB
   power, one serial owner, and save the full transcript. No backup/export of
   disposable sessions is required by the user's deletion decision.
2. Wait for recovery scan and GPS startup to finish. Send the following one at
   a time and verify each response. STOP commands below affect RAM pipelines;
   a surprising active V1 recorder is a reason to halt this procedure and review.

   ```text
   RECORD STATUS
   OBS STOP
   COMP STOP
   OBS STATUS
   COMP STATUS
   TCB STATUS
   GPS STATUS
   DIAG ID
   ```

   Require RECORD STOPPED, OBS OFF/fast OFF, COMP OFF/fault=0/queues empty,
   TCB DISABLED, GPS profile verified, unchanged identity/SFDP, SR1=00/SR2=02.
3. **Only after execution approval**, send `RECORD ERASE`, read its warning, then
   send `RECORD ERASE CONFIRM` within 30 seconds. These are proposed commands,
   not commands executed in this phase.
4. Keep power connected. Poll `RECORD STATUS` conservatively, e.g. every 5 seconds,
   and retain errors. RECORD STOP does not cancel an erase. Do not start other
   diagnostics, recording or export. There is no current per-sector progress
   address in STATUS; next_sector remains the cached append value until completion.
5. Require the explicit completion line and STOPPED/next_sector=0. Query DIAG ID
   again for stable identity/status. Do not infer completion merely from silence
   or USB disconnect. A reboot/recovery scan is read-only if needed after success.

Total internal erase time from candidate tSE values is approximately 5.46 minutes
typical or 54.6 minutes at the table maximum, **plus** readback/scheduling costs.
Reading 16,773,120 bytes at ideal 1 MHz wire speed alone takes about 134.2 seconds
before command/software overhead. Allow a long supervised maintenance window;
do not impose a two-minute host timeout on partition erasure. The firmware's
5-second limit applies to each sector, not the whole chip.

On fault, stop issuing mutation commands and save the transcript. Do not reset
or power-cycle a potentially busy flash as a routine cancellation. Interrupted
erasure is irreversible and may leave a partly erased partition; recovery does
not automatically finish it or reclaim all holes. A further erase requires a
new deliberate review/confirmation after the fault is understood.

## TCB3 activation and unresolved gates

Current `TACTICPOD_TCB3_FLASH_ENABLED` defaults to **0** and is not enabled in
platformio.ini. The uploaded ELF matches the Phase 3B.4 tested normal artifact.
TCB START cannot record in that build. A future dedicated build must explicitly
define `TACTICPOD_TCB3_FLASH_ENABLED=1`, retain all existing board/IMU build flags,
run enabled integration tests and record its ELF/ZIP hashes before an approved
upload. No enabled physical build was created or uploaded in this review.

The enabled implementation checks IMU readiness, five matching JEDEC reads,
idle V1/diagnostics/GPS ownership, and empty/stopped compressor RAM. It scans
read-only before append, requires whole blank sectors and blank destination
pages, checks all bounds again in the SPI adapter, and performs one aligned
256-byte PP with a single CS assertion. Four 64-byte foreground slices do not
constitute four program operations. BUSY is polled every 2 ms with a 1-second
program timeout, followed by complete readback and CRC/seal verification before
releasing a retained output block. It does not erase, resume or record at boot.

Remaining gates before a physical recording:

* Review the exact-part/provenance uncertainty and applicable held-CS SCLK-gap
  and power-failure requirements. The B-compatible fingerprint is strong family
  evidence, not an electrical qualification of the specific die.
* Review status guarding: the writer checks SR1 but not family-specific SR2
  suspend/protection state. It does not attempt configuration changes. A later
  approved preflight/guard change may be appropriate; no such change is made here.
* Prepare a binary-safe host capture and independent semantic validator, then
  rehearse them against fixtures. The decoder exists, but it is not a USB serial
  download client and its successful exit alone does not certify zero issues.
* Fix the test's acceptance/abort limits and approve an enabled build/upload
  separately. Physical SPI service overhead and full-system RAM high-water are
  unmeasured. This first test must characterize them, not assume model values.

## Proposed two-minute recording

After the above gates and successful erase, use a separate approved upload of
the enabled build. A normal boot must still scan read-only and keep recording
off. Repeat stopped-state/identity/GPS checks; observe rates/fix readiness before
starting. Keep live IMU printing off with `IMU OFF`.

Keep **OBS STOP** throughout the recording; do not issue OBS START or COMP START.
TCB START owns and starts the existing compressor, which independently requests
100 Hz IMU acquisition and receives sentence-scoped GPS observations even with
OBS diagnostic queues off. This avoids their small retained queues overflowing.

1. Send `TCB START` once. Wait for `TCB STARTED` after the append scan; an initial
   `START scanning` notice is not capture-start confirmation. Require session 1
   when genuinely starting in an empty partition. Any refusal/fault aborts.
2. Use a host monotonic timer to request `TCB STOP` 120 seconds after STARTED.
   Keep the board powered and connected. There is **no built-in two-minute
   duration limit**: USB loss does not stop acquisition; the host must send STOP.
   A requirement for an autonomous hard duration/address fence would require a
   future reviewed implementation, not an invented existing command.
3. During capture, sample `TCB STATUS` and `COMP STATUS` about every 2 seconds,
   allowing each report to finish. GPS STATUS can be checked at low frequency.
   V1 remains stopped. Do not export, run GPS configuration, reset queues or
   invoke DIAG ID during capture.
4. TCB STOP seals/drains partial blocks and writes a footer. Wait for **TCB
   STOPPED**, not merely `STOP requested`, then wait for the legacy append-cache
   rescan to finish. Require COMP OFF/empty, RECORD STOPPED and no errors.

The nominal target is about **12,000 IMU, 1,200 RMC, 120 GGA**, around 60 two-second
blocks. USB start/stop latency, first/last epoch boundaries and source availability
mean these are not guaranteed exact physical counts. Pass criteria must reconcile
the exact captured counts, footer accounting and timestamp coverage instead of
fabricating samples to meet nominal totals. Invalid GPS fixes remain records.

For budget planning, 60 hard-maximum 8228-byte frames require 2340 fragment pages
and 168 sectors (688,128 bytes), including reserved header/footer pages. A partial
extra block can add sectors; reserve at least 1 MiB for this test. Actual mixed
high-entropy simulated sensor data used 245,760 bytes for 120 seconds. Neither
number is a 300-minute capacity guarantee or a limit enforced by the device.

## Fault and acceptance criteria

| Evidence | Required response |
|---|---|
| TCB FAULT, state Fault, error other than none, unexpected reset/memory fault | Stop the test; preserve logs/RAM/flash; do not auto-erase/retry |
| rejected, missed, CRC, incomplete-write or full counter nonzero | Fail; stop capture where the engine still permits orderly drain |
| COMP fault, increasing rejects, retained output saturation or stalled commits | TCB STOP; diagnose throughput/backpressure before another run |
| One backpressure slice without loss | Record it; inspect queue trend, not automatically label it lost samples |
| Maximum block service time approaches/exceeds the two-second producer interval | Stop and review queue margin; reported max_block_ms excludes some waiting/compression time |
| Acquisition interval >=20 ms or non-monotonic IMU timestamps | Fail acquisition continuity; missed-deadline firmware counters are also required zero |
| GPS checksum/malformed/UTC-epoch gaps or rate deficit | Investigate receiver availability versus UART/firmware loss; no invented replacement positions |
| Invalid GPS fix or unavailable UTC/quality | Preserve masks/fields; do not count it as a valid navigation solution |

Record max_service_us, COMP max_slice_us/work time, input/output high-water and
backpressure. The current STATUS command does not expose all physical stack/heap
peaks or every per-stream timing/count field; derive sample timing/counts from
exported records/footer. Do not claim actual hardware WCET from SPI wire-time
simulation. Classify a short-run success separately from sustained/temperature/
power-cut qualification. No deliberate power cut belongs in this first test.

## TPX3 export and independent verification

With recording completely stopped, send **TCB READ** and capture raw USB bytes
to a new `.tpx3` file, from the TPX3 BEGIN packet through the validated END packet.
Do not use line decoding, newline conversion or a text serial monitor to save
binary. Do not send interleaved commands or treat an idle read timeout as END.
The exporter suppresses competing text, exports read-only and excludes the
diagnostic sector. Capture a second export and compare byte hashes; unchanged
storage should produce identical packets.

Decode the local file with the existing tool (raw output must be a new path):

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" tools/tcb3_decode.py first-session.tpx3 --raw-output first-session.v2wire > first-session.json
```

Require all export/root/session issue lists empty, container 3.2.0, exactly the
intended session, valid metadata/sector/page/block sequences, clean final footer,
zero loss/reject/missed/CRC/incomplete/full counters and per-type decoded counts
equal to committed=offered. TPX3 packet CRCs, page CRC/seals, fragment continuity
and complete frame CRCs must all validate. Decoder issue lists matter even when
the CLI exits zero.

Independently compute per-stream counts, first/last timestamps, interval
min/max/percentiles and long-run rates; require shared uint64 timestamps ordered
nondecreasing across streams and strictly advancing within each sensor stream.
Expect about 100/10/1 Hz with endpoint tolerance, not a blind exact count test.
For GPS, verify sentence-specific validity and UTC advancement, including date/
day rollover, and preserve absent fields. Review raw int16 axes without changing
their precision or labeling repeated physical readings as invented samples.

An independent original sensor-byte trace is not currently exported during
capture. Two identical exports plus counts/CRCs establish consistent persistence
and decoding, not direct byte-by-byte comparison to an independently observed
physical input stream. Codec reconstruction is separately tested byte-exact
against host fixtures.

Finish with TCB STOPPED, COMP/OBS off, RECORD STOPPED, final status evidence and
the USB port released. Store firmware identity and export hashes with the data.
V1 RECORD READ remains its original IMU-only CSV path. Legacy V1/V2 decoding and
3.1/3.2 container tests remain required even after old physical sessions vanish.

## Host validation performed for this review

No firmware source/configuration edits were made. The review adds only this
plan, logs and a full-chip erase simulation. Existing suites are invoked with
the PlatformIO Python, with exact commands/results in
`logs/phase3b5-review/regressions.json`:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" tests/gps/run_tests.py
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" tests/recording/run_tests.py
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" tests/flash_write/run_tests.py
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" tests/recorder_v2/run_tests.py
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" tests/storage_benchmark/run_tests.py
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" tests/compact/run_tests.py
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" tests/compact/test_decoder.py
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" tests/compact/test_timing_model.py
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" tests/compact/test_resources.py
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" tests/tcb3/run_tests.py
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" tests/tcb3/integration_tests.py
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" tests/tcb3/console_tests.py
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" tests/flash_identity/run_tests.py
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" tests/tcb3/sustained_tests.py
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" tests/tcb3/long_integration_tests.py
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" tests/recording/erase_review_tests.py
```

**All 16 suites passed**, including five new erase-review scenarios. The
120-second integrated run recovered 12,000 IMU, 1,200 RMC and 120 GGA samples
byte-exact with zero modeled losses/missed deadlines. Sustained maximum-frame
tests again filled the recording partition without growing backlog under their
declared timing assumptions. These are host simulations, not physical erase or
program qualification.

The added test runs the unchanged production erase state machine against a full
16 MiB RAM NOR: exactly 4095 ascending 20h operations, 4095 WREN, no page programs,
every recording byte FF, all 4096 protected canary bytes unchanged, and final
recording stopped. Its additional cases verify erase readback rejection, denied
WEL, changed JEDEC and WEL retained after completion, with the protected canary
unchanged on each failure. Existing suites additionally cover erase timeout,
readback failure, confirmation expiry, protected boundaries, mixed formats,
interrupted programs, full storage, corruption, queue pressure and acquisition.
Detailed final pass/fail evidence is saved with the review before completion.
