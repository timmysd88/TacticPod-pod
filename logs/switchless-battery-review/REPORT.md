# Switchless first-battery-test firmware preparation

**Firmware ready for review; GO for a separately authorised USB-only bench
stage. Physical LiPo operation remains electrically unqualified.** No upload,
serial access, battery connection or physical flash write/erase was performed.
The installed firmware, board, bootloader, charger and existing recordings remain
unchanged. No switch, extra circuit or automatic recording is introduced.

Exact first-test steps/commands:
[switchless-battery-first-test.md](../../docs/switchless-battery-first-test.md).
That document clearly separates the USB-only stage from the conditional battery
connection/charging stage; neither is automatically executed by this work.

## Firmware result

The existing corrected `tacticpod_reusable_usb` already implements the requested
flow. No active firmware/configuration change was necessary. A reviewed artifact
is archived as **tacticpod_reusable_usb-switchless.zip**.

SHA-256: **ffee03178a49e2b77130da9afdefd5f5c358c6b22ecb3e1e249b2157ca540c67**

ELF SHA-256: c6f09813857a70bb38fafaa27ef16c1957ae6b63584793d8302bf7afebbc3654

Both reusable and default environments build successfully. Static RAM is
**120836 /237568 bytes**; program **189424 /811008 bytes**. This adds no firmware
RAM/program storage. The ZIP is application-only; no bootloader/SoftDevice image.
Default TCB3-disabled ELF remains byte-identical to its previous baseline.

Verified requirements:

- Corrected P0.14 LOW-latch-before-OUTPUT runs before setup; linked ELF regression
  confirms it. P0.13 remains INPUT/no pulls for proposed nominal 50mA.
- No wait-for-Serial in setup. IMU, flash read-only scan and GPS startup initialise
  without a monitor. GPS startup progresses without USB; absent communication
  fails with a bounded timeout and settings remain safe.
- Existing `TCB DURATION 1..1800`, `TCB START`, `TCB STOP`, `TCB STATUS`, `TCB READ`
  suffice. No serial command was invented. Duration defaults to 120 seconds,
  is volatile, begins after STARTED and stops capture independently of USB.
- Finalisation has a further 60-second watchdog. Loss, flash-full, queue overflow,
  CRC/write fault or missed deadline is reported/locked rather than hidden.
- No automatic START at boot, unplug or reconnect. Normal 115200-baud connection
  changes do not call stop/reset or erase. USB export is read-only and separate
  from recording; legacy RECORD READ remains unchanged.
- BAT STATUS exposes ADC_UNVALIDATED, calibration/freshness/trust, invalid/timeouts,
  voltage estimates and action flags. Cell presence is not inferred. Uncalibrated
  readings cannot be presented as measured battery accuracy or arm low shutdown.
  Existing armed warning/finalisation policy and thresholds remain intact.
- V1/V2 formats, lossless TCB3, raw data, RAM-only GPS configuration, partition and
  existing-session protection remain unchanged.

## Tests run

Commands:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e tacticpod_reusable_usb -e seeed_xiao_nrf52840_sense
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" logs/switchless-battery-review/run_validation.py
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" tests/gps/run_tests.py
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" tests/reusable/download_tests.py
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" logs/switchless-battery-review/package_review.py
```

**Ten current targeted suites passed; zero failed** (nine-suite runner plus GPS
suite). The downloader suite was rerun after adding the offline verifier, and
passed. The unchanged production firmware's prior complete 26-suite result is
preserved; a fresh full storage/compressor suite was not claimed. The ordinary
regression runner now also includes the two new switchless/download suites.

The two new switchless production-code simulations cover cold boot with no
monitor, no automatic writes/start, USB command start, ten-second automatic stop
while disconnected, repeated connection changes during capture, reconnect after
completion, repeated exports, duration boundaries 1/1800 and refusal of 0/1801,
and preservation of all old sessions and diagnostic-sector bytes.

| Simulated ten-second case, both USB patterns | Result |
| --- | --- |
| New session | 58, starts 0x039000 |
| Counts IMU / RMC / GGA / EVENT | **1001 /100 /10 /0** |
| Capture stop elapsed | **10000096us** |
| Lost/rejected/missed/CRC/incomplete/full counters | **All zero** |
| Allocated new sectors | **0x039000-0x03DFFF**, next 0x03E000 |
| Programs | 75 page programs, zero erases |
| Model max flash service slice / block persistence | **544us /356ms** |
| IMU timestamp spacing | **9708-10084us** |
| Export | 277824 bytes, two identical downloads per case |
| Export SHA-256 | ee131a3bebd5c3bdf2a4972e8f8aca4eba5a6383d9221c20ded37bb8d537721c |

The first shorter IMU interval reflects entering the millis-based fast schedule;
it is retained. No sample/timestamp is discarded, fabricated or retimed to force
1000 samples. Device timestamp order and byte-for-byte canonical reconstruction
were verified. These timings use modelled 1MHz SPI wire cost, 10ms program busy
and sensor cost; they are **not measured battery-powered timings or WCET**.

The other current suites cover battery calibration gates, invalid/stale readings,
simulated drain and hard/soft finalisation, low-voltage recovery, console guards,
retained sessions, interrupted-page recovery, finalisation timeout, TIMER4 rollover,
overflow/fault locking, GPIO startup and real ADC register sequencing. GPS tests
include automatic startup without USB and 100Hz acquisition concurrency.

## Flash and export safeguards

Reusable program window: **0x039000-0xFFEFFF**. Existing three-session prefix below
0x039000 is protected; diagnostic sector **0xFFF000-0xFFFFFF** excluded. Full-sector
and page blank verification, one page-program operation, busy/WEL checks, CRC/
seal validation, append recovery and fault locking are unchanged. No erase is
introduced. Reusable mutation guards reject legacy erase/destructive diagnostics.

The conservative ten-second admission budget is 17 sectors under raw fallback;
this is not a separate session fence. Actual compressed use varies. A real test
must verify the pre-test export matches the archived 1/3/12 sessions and require
the expected STARTED session58 before continuing. If another session exists,
stop for review rather than erase or silently accept a different baseline.

New host tools:

- tools/tcb3_download.py: explicit --port invocation only; opens 115200, sends only
  TCB READ, saves exact raw bytes with exclusive-create, validates packet CRC/
  framing, retains partial failures, closes serial and never retries automatically.
- tools/verify_tcb3_append.py: offline before/after check for unchanged committed
  prefix/old sessions, exactly one complete new session, page/block/packet integrity,
  footer counters, timestamp order/range, capture duration, counts and IMU gaps.
  It does not equate GPS sentence count with valid navigation.

Example simulated export and independent CLI decoder/verifier output are archived
as simulated-switchless.tpx3, simulated-decoded.json and simulated-verified.json.
All new observations there are synthetic; the original three sessions are the
archived physical fixture. These are not new physical recordings.

## First physical test, after review

1. Battery absent: verify ZIP hash; upload application ZIP using the proven
   nrfutil method; reconnect at 115200 and complete stopped-state/GPS/IMU/flash
   preflight. Export/decode baseline before.tpx3 and confirm prior sessions.
2. Only after separate electrical-risk review, fit the verified stock battery
   connection while USB is absent and recording stopped. Let it boot, attach USB
   at 115200 and repeat stopped checks. USB attachment can immediately charge.
3. With no independently measured calibration, retain BAT MODE USB and document
   voltage protection is unarmed. Do not fake calibration or call this qualified
   battery-protection operation. If real calibrated CELL mode is selected, require
   fresh trusted data and respect any refusal.
4. Issue TCB DURATION 10 then TCB START once. Wait for STARTED/session58, unplug
   USB after about two seconds, keep power/wiring stable and wait until at least
   80 seconds after STARTED. No reset, second START or power-cut experiment.
5. Reconnect at 115200; require clean stopped state, session58, lock=0, drained
   queues and no faults. Export twice, compare hashes and run the offline verifier
   against the saved baseline. Release COM8. No erase/retry on any failure.

Full exact commands and stop conditions are in the linked procedure.

## Remaining risks/limitations

- Corrected application startup does not eliminate reset/bootloader P0.14/P0.31
  electrical uncertainty. Installed bootloader identity/source match remains
  unknown; available historical source resets pins to high impedance. No scope
  measurements or manufacturer confirmation were obtained.
- Firmware cannot guarantee physical USB-to-battery handover, sufficient battery
  current, wiring continuity or lack of brownout. A complete continuous session
  demonstrates its recorded interval, not electrical qualification or proof that
  no reset happened after completion. No persistent boot counter was added.
- TinyUSB's existing **1200-baud DTR-close path invokes DFU/reset**. It remains
  needed for the established uploader; never use it during recording. Normal
  tests/downloads use 115200. Historical bootloader source can wait indefinitely
  after deliberate double-reset DFU; do not enter DFU during a battery capture.
- Nominal 50mA is not measured charging current. The cell, polarity, charger
  thermal behaviour and pack protection claims remain unqualified. USB presence
  and a plausible ADC voltage do not establish a connected or safe cell.
- BAT MODE USB permits recording without trusted voltage but disables voltage
  decisions. CELL requires real calibration; unavailable/stale data cannot serve
  as dependable low-voltage protection. Both limitations are explicit.
- There is no electronic power switch/shutdown or standby-current qualification.
  Timed stop is not power-off. Battery disconnection while writing loses in-flight
  data; completed append-only pages remain recoverable within existing guarantees.

## Files changed

No active firmware source, PlatformIO configuration, vendor core, bootloader,
binary format or partition changed. Added tests/reusable/switchless.cpp,
switchless_tests.py, download_tests.py; tools/tcb3_download.py,
tools/verify_tcb3_append.py; docs/switchless-battery-first-test.md; and this review's
logs/scripts/artifacts. Updated tests/reusable/run_all_tests.py and linked the
new procedure from docs/reusable-prototype.md. Source/artifact/archive hashes and
exact regression exits are in summary.json/all-results.json.
