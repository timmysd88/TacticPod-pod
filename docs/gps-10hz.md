# Reversible MAX-M10S SPG 5.10 RAM-only profile

**Startup integration built and simulated; not uploaded.** The user previously
verified this exact profile on hardware: 150 RMC messages in 15 seconds with
zero checksum failures. USB console stays at 115200; hardware Serial1 stays
at 38400, 8N1, D7 RX / D6 TX.

## Automatic startup

Each firmware startup waits for checksum-valid NMEA at 38400 baud and idle
recording, flash recovery and UART diagnostics. It then uses the same MON-VER,
snapshot, preflight, four-stage RAM-only transaction and complete readback as
GPS 10HZ TEST. No fix or USB connection is required. IMU acquisition, LED
service and reception continue. Recording cannot start during configuration;
startup defers if recording is already in progress.

Without valid NMEA within 30 seconds, automatic configuration is skipped and
GPS STATUS reports the communication error. No writes are sent. Fix the link
then use explicit GPS 10HZ TEST or restart the board. Read-only requests get at
most two retries on timeout within the 60-second job deadline. Writes are never
blindly retried: missing/negative ACKs trigger bounded rollback and readback.
Failure details remain in GPS STATUS. If recovery cannot be verified, status
says UNKNOWN and the original snapshot is retained for GPS RESTORE. A broken
UART can prevent recovery; reception continues at whatever settings remain.

Startup releases its recording interlock when verification/recovery finishes,
even without USB. Queued reports wait for USB and are suppressed during CSV
export. GPS STATUS shows the last verified RAM profile; GPS RATE measures live
cadence. Independent receiver resets can invalidate that last verification.

GPS RESTORE restores the pre-startup snapshot and leaves it restored for the
rest of that boot. An accepted explicit TEST or RESTORE cancels any pending
startup job. The next board startup applies 10Hz again. The snapshot exists
only in board RAM and is lost on board reset. If an independently powered GPS
already matches 10Hz, startup validates all keys and guards without writing,
reporting the original snapshot unavailable; RESTORE cannot reconstruct it.

A GPS power cycle that removes RAM retention reloads its existing persistent
or default settings. This firmware never saves to BBR, flash or OTP. If the
board also restarts, startup reapplies 10Hz and captures that baseline. A GPS-only
power cycle does not restart the board's one-shot startup job: restart the board
or use GPS 10HZ TEST (restore any retained snapshot first). Use GPS CONFIG and
GPS RATE to inspect receiver settings and cadence after an independent reset.

## Commands after upload/execution approval

Wait for `RECORD scan complete` and ensure `RECORD STATUS` says STOPPED. Send
`GPS 10HZ TEST` followed by Enter. The firmware first polls MON-VER and requires
an advertised `FWVER=SPG 5.10`, then captures all 22 writable-profile settings
and seven read-only guards from the current GPS RAM layer. Any unavailable,
rejected, invalid or timed-out snapshot field aborts before sending a VALSET.

The UART must already be enabled at 38400, with UBX input, UBX output and NMEA
output enabled. Power mode must be FULL (0); the command refuses power-save
operation rather than changing its settings. The measurement/navigation values
must be nonzero, with navigation ratio in 1-127. The navigation time reference
is preserved. Other GPS/UART diagnostics and recording/flash operations must be
idle. Both TEST and RESTORE are refused during IMU recording, flushing, startup
scan, export, erase, faults or another diagnostic.

Only after the complete snapshot passes these checks does the firmware apply
the table below. It uses official UBX-CFG-VALSET version 1 transactions with
`layers=0x01`: RAM only. Four packets START, CONTINUE, CONTINUE and empty APPLY
stage and commit the complete profile together. All key/value pairs are sent
before APPLY: SPG 5.10 ignores pairs attached to APPLY. There are no intermediate, applied constellation
combinations. Each packet fits one 64-byte BSP DMA batch; reads are never
interleaved into a staged transaction because VALGET would cancel it.

Wait for `GPS PROFILE: 10HZ TEST VERIFIED` and `GPS PROFILE END`. Every one of
the 22 written keys and seven guards must match readback. The result prints
saved, requested and actual test/restore values for each key, plus per-stage
ACK/NAK/timeout status. It verifies the configuration,
not actual navigation/fix cadence. The existing `GPS RATE` command can then
measure GGA and RMC separately, including indoors without a fix. `GPS STATUS`,
RAW, CONFIG, SCAN and normal reception remain available after completion.
Normal GPS printing remains controlled by GPS ON/OFF; a profile command does
not start recording or enable GPS printing automatically.

Send `GPS RESTORE` to apply the saved 22 values in another RAM-only transaction
and read back all 29 fields. `RESTORE VERIFIED` means every value matched. The
snapshot is then released, allowing a fresh TEST. A second TEST cannot replace
an existing snapshot. RESTORE without a snapshot sends nothing. A receiver
already matching the test profile without a retained snapshot is refused,
because the original settings may have been lost after a XIAO-only reset.

## Exact profile

All constellation/signal names below have the `CFG-SIGNAL-` prefix. Message
names have the `CFG-MSGOUT-NMEA_ID_` prefix and apply only to UART1.

| Configuration key | ID | RAM test value |
| --- | --- | --- |
| GPS_ENA | `0x1031001F` | 1 |
| GPS_L1CA_ENA | `0x10310001` | 1 |
| GAL_ENA | `0x10310021` | 1 |
| GAL_E1_ENA | `0x10310007` | 1 |
| BDS_ENA | `0x10310022` | 0 |
| BDS_B1_ENA (B1I) | `0x1031000D` | 0 |
| GLO_ENA | `0x10310025` | 0 |
| QZSS_ENA | `0x10310024` | 1 |
| QZSS_L1CA_ENA | `0x10310012` | 1 |
| SBAS_ENA | `0x10310020` | Saved value |
| SBAS_L1CA_ENA | `0x10310005` | Saved value |
| BDS_B1C_ENA | `0x1031000F` | Saved flag; inactive while BDS_ENA=0 |
| QZSS_L1S_ENA | `0x10310014` | Saved value |
| GLO_L1_ENA | `0x10310018` | Saved flag; inactive while GLO_ENA=0 |
| CFG-RATE-MEAS | `0x30210001` | 100 ms |
| CFG-RATE-NAV | `0x30210002` | 1 measurement per navigation cycle |
| RMC_UART1 | `0x209100AC` | 1: every navigation epoch, requested 10 Hz |
| GGA_UART1 | `0x209100BB` | 10: every tenth epoch, requested 1 Hz |
| GSA_UART1 | `0x209100C0` | 0: disabled |
| GSV_UART1 | `0x209100C5` | 0: disabled |
| GLL_UART1 | `0x209100CA` | 0: disabled |
| VTG_UART1 | `0x209100B1` | 0: disabled |

QZSS L1 C/A is retained/enabled as recommended by the MAX-M10S integration
manual when GPS L1 C/A is enabled. SBAS and other supporting flags are captured
and restored exactly. All 14 supported constellation/signal flags are included
so the complete previous signal combination is restored atomically.

These seven guards are captured and verified but **never written**:

| Guard | ID | Required/preserved value |
| --- | --- | --- |
| CFG-UART1-BAUDRATE | `0x40520001` | 38400 |
| CFG-UART1-ENABLED | `0x10520005` | 1 |
| CFG-UART1INPROT-UBX | `0x10730001` | 1 |
| CFG-UART1OUTPROT-UBX | `0x10740001` | 1 |
| CFG-UART1OUTPROT-NMEA | `0x10740002` | 1 |
| CFG-RATE-TIMEREF | `0x20210003` | Saved time reference |
| CFG-PM-OPERATEMODE | `0x20D00001` | FULL (0) |

No UART baud, input protocol, output protocol, time reference, power mode,
dynamic model, CPU clock, OTP, BBR or GPS flash settings are changed. Existing
read-only GPS CONFIG still supports its original diagnostic queries. The new
profile commands never read or write undocumented CPU/OTP keys. They never
access TacticPod SPI flash or change its recording format, memory layout or
erase/write protections.

## Timing, failures and rollback

Reception uses the existing single UART reader and checksum-valid bounded UBX
demultiplexer. NMEA parsing, idle IMU acquisition and LED servicing continue
during the operation. Afterward, the existing 100 Hz IMU recorder can run with
10 Hz RMC input; this combination is covered by simulation.

There is one outstanding request, a 1200 ms reply deadline, a 1000 ms TX-ready
deadline and 200 ms gaps between ordinary requests. Signal configuration causes
the receiver to restart its GNSS subsystem, so verification waits 600 ms after
the commit ACK, exceeding u-blox's required 500 ms margin. This can interrupt
navigation temporarily even though settings are reversible. Acquisition never
waits in a busy loop and no blocking UART flush/reinitialisation is used.

TX lengths differ between consecutive requests. Reads alternate single and
duplicate-key polls; VALSET can repeat its last key with the same value on a
retry. These official duplicate semantics avoid mistaking the previous DMA
byte count for a new completion. Duplicate pairs are used only in data stages;
APPLY always has an empty data list and a 12-byte UBX frame. Packets remain at
most 62 bytes. A stuck TX
prevents another BSP write from being attempted.

A normal TEST takes roughly 13 seconds. Each test/restore attempt has a
60-second operation deadline; an automatic rollback gets its own deadline.
USB reports wait for available space/reconnection without delaying collection,
verification or automatic recovery. Recording/flash operations stay interlocked
until the result is delivered; GPS reception continues.

If TEST fails after any possible VALSET transmission (NAK, missing ACK, short
write, timeout or incorrect readback), firmware automatically starts one fresh
transaction restoring the entire saved profile. Restarting the transaction
discards uncommitted staged settings. If a restore write itself fails, readback
polls cancel any pending transaction and inspect all fields where communication
allows. Lost ACKs do not prove failure if every saved value is subsequently
verified. ACKs alone never prove success.

Diagnostics capture each mismatch's key, requested value, actual value,
timestamp and age since the commit ACK **before** scheduling rollback. The
nonblocking trace prints it before the rollback event when USB is available.
Rollback never waits for a stalled or disconnected USB console; deferred traces
keep their capture timestamps. A persistent first-mismatch record and separate
test/restore readback arrays preserve the evidence even if the bounded 64-event
trace queue overflows. `trace_dropped` reports lost trace events; they cannot
erase the first mismatch record. Each of four TEST and RESTORE transaction
stages reports bytes, key/value count, ACK/NAK/timeout and reply time. Unexpected
VALSET acknowledgements are counted/logged without advancing the transaction.
Unknown commit age is labelled UNKNOWN when its ACK was lost.

If restoration cannot be fully verified, the report says
`RESTORE FAILED/UNVERIFIED` and retains the original snapshot for another explicit
`GPS RESTORE`. It never silently declares success, replaces the snapshot or
loops indefinitely sending configuration writes. Hardware TX faults can prevent
rollback entirely until communication recovers. UBX ACKs contain class/ID rather
than a transaction sequence number, so severely delayed replies remain
ambiguous; spacing plus complete key/value readback provides the final check.

The rollback snapshot is stored **only in XIAO RAM**. It is lost on XIAO reset,
power loss or reflash, while an independently powered GPS may retain the RAM
test configuration. In that case GPS RESTORE refuses because it cannot recreate
the original snapshot. Power-cycling/resetting the GPS reloads its existing
persistent/default configuration; that may differ from pre-test RAM-only changes.
No automatic GPS reset or persistent save is sent. Do not use another GPS
configuration writer concurrently: the snapshot assumes this firmware is the
only writer, and does not contain unrelated settings or an entire receiver dump.

## Anticipated UART bandwidth and 10 Hz limits

38400 baud with 8N1 carries at most **3840 payload bytes/s**. The requested stream
is ten RMC sentences plus one GGA sentence each second. With illustrative
80-byte sentences (including delimiters/checksum/CRLF), that is
`10*80 + 1*80 = 880 bytes/s`, about **23%** of capacity. Allow roughly
**0.9-1.1 kB/s (23-29%)** for this pair with varying fields/sentence lengths.
These are planning estimates, not measured output from this new profile.

Other NMEA and UBX message settings are unchanged, so any additional enabled
output adds traffic and may increase with the navigation rate. Poll responses
and ACKs add temporary traffic. Use GPS RATE after an approved test to measure
the actual complete stream and cadence. No UART speed increase is requested.

The MAX-M10S data sheet lists GPS+Galileo at up to 10 Hz with the default clock,
so no high-performance CPU/OTP programming is needed for this requested setup.
This is a typical-condition navigation limit, not a guarantee of every fix or
indoor position availability. Readback of 100 ms/1 confirms the requested rate;
actual advancing NMEA epochs and valid fixes require separate measurement.

## Official sources and simulation

- [M10 SPG 5.10 interface description, UBX-21035062 R03](https://content.u-blox.com/sites/default/files/u-blox-M10-SPG-5.10_InterfaceDescription_UBX-21035062.pdf): VALGET, RAM layer mask, version 1 VALSET transactions, cancellation/duplicate semantics and exact keys.
- [MAX-M10S integration manual, UBX-20053088 R04](https://content.u-blox.com/sites/default/files/MAX-M10S_IntegrationManual_UBX-20053088.pdf): QZSS support recommendation, message output and navigation configuration.
- [MAX-M10S data sheet, UBX-20035208 R08](https://content.u-blox.com/sites/default/files/MAX-M10S_DataSheet_UBX-20035208.pdf): constellation-dependent navigation limits.

Run the normal PlatformIO build and all three existing regression runners:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" tests/flash_write/run_tests.py
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" tests/recording/run_tests.py
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" tests/gps/run_tests.py
```

The GPS suite runs actual ARM firmware against a stateful simulated receiver
with staged transactions and a separate configuration database. It validates
outgoing checksums, RAM-only layer masks and exact allowed keys; applies values
only at commit; and checks exact restoration against the original database.
New cases cover successful test/restore, missing snapshots, recording/diagnostic
interlocks, firmware/baud/power preflight rejection, snapshot failures, rejected
and lost acknowledgements, readback mismatch/timeout, failed rollback and retry,
short/stuck TX, USB disconnect/backpressure, timer wrap, fragmented/corrupt UBX,
supporting constellation flags, and 10 Hz GPS with 100 Hz IMU recording.
The user has verified the profile's 10Hz stream on hardware. The new automatic
startup integration still requires hardware validation after an instructed upload.

See [the failed-test investigation](gps-10hz-investigation.md) for the original
APPLY-payload defect, its reproduced failure and the corrected wire sequence.
