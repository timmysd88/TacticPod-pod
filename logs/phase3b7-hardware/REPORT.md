# Phase 3B.7 first physical TCB3 recording: PASS

One supervised five-second recording completed and stopped automatically. No
manual STOP, second START, retry, external erase, power-cut experiment or
unrestricted firmware was used. COM8 was closed after verification. Firmware
and configuration sources were not changed during this test.

## Artifact, startup and blank preflight

Uploaded only `.pio/build/tcb3_first_write/firmware.zip`, by the proven 1200-baud
touch / COM7 bootloader / Adafruit nrfutil application ZIP single-bank procedure.
The board returned to COM8. Exact tested hashes:

* ELF: `eae2df98fbc2418a185941db2389a7fee6ca3395befa2c9f4eb1d3fb9ae085d4`
* ZIP: `a8d58032d620e320adfed80be8462aee2b646cf6b4597c2c93182c0f5851ae44`
* Application: `64ef63d6b4622eb676b462b7ff8d322e1b3158851c929fa4dca7e143fb3d0723`

All 17 recorded regression suites passed; source hashes and all artifact hashes
matched before upload. The linked artifact and runtime STATUS both identify
`test=5s,64KiB,15s`: five-second capture, 15-second finalisation deadline from
capture start, writes only below exclusive address `0x010000`. V1 mutation and
all erase commands are disabled in this build. No recording occurred at boot.

USB, IMU initialisation and read-only flash recovery scan completed without an
error. JEDEC was 0B4018 on five repeated reads; SFDP matched the previously saved
256-byte fingerprint in both passes and advertised 16 MiB. SR1=00 before/after,
SR2=02, legacy ID=0B17. GPS startup reported `10HZ ALREADY VERIFIED`; GPS STATUS
confirmed the 100 ms MEAS/NAV=1 RAM profile, 38400 UART and valid fix.

V1 was STOPPED with append position zero. OBS and COMP were explicitly stopped
and checked before START. OBS's default passive boot queues had pre-existing
RMC/GGA overflow counters 192/14 before stopping; these did not increase during
the recording. They are not recording-path losses. COMP had no retained data,
no fault and empty input/output queues.

An initial read-only TCB READ returned a valid empty BEGIN/END stream with no
issues: no occupied header/session was found. This fresh scan and append-zero
check corroborate the earlier full 4095-sector all-FF erase verification.
They are not a fresh host dump of every payload byte. During recording, the
firmware additionally verified each allocated sector/page blank before writing.

## Recording and integrity

Exactly one TCB START was sent. Firmware autonomously stopped capture, drained
three frames and committed a clean footer. Every offered observation committed:

| Stream | Offered | Committed | Lost |
|---|---:|---:|---:|
| IMU_RAW | 500 | 500 | 0 |
| GPS_RMC | 50 | 50 | 0 |
| GPS_GGA_QUALITY | 5 | 5 | 0 |
| EVENT | 0 | 0 | 0 |

Rejected, missed deadlines, full, CRC failure and incomplete-write counters were
all zero. COMP fault=0, rejects=0, backpressure_slices=0 and raw_fallbacks=0.
Input high-water was 2/2 (filling plus processing slots); output high-water 1/2.
All queues drained to zero. No reset, brownout evidence, protection fault,
timeout or flash verification failure was observed. This is observed behavior,
not an instrumented supply-voltage trace.

The two raw exports are byte-identical, each **9024 bytes**, SHA256:
`e003fc70802374e1559c22cacc0930f5b1d58209da1868263e42126d87d5a972`.
Both contain 34 valid-CRC TPX3 packets (BEGIN, 32 page packets, END). Host checks
validated 23 nonblank page CRCs/seals, all fragment/page/sector/block sequences,
all three frame CRCs, session identity and clean footer accounting. Export/root/
session issue lists are empty; nine exported unused pages remain FF.

Standalone `tools/tcb3_decode.py` successfully decoded the physical export to
14,400 bytes of reconstructed typed records. Container is 3.2.0, session 1,
clock 1 MHz, IMU target 100 Hz/ODR104, sensitivities 61 ug and 8750 udps per LSB,
RMC/GGA targets 10/1 Hz, UART38400. Device ID is present; build ID is explicitly
unavailable in session metadata and is supplied by the external artifact log.
No V1/V2 or container schema was changed.

## Measured timing

| Measurement | Result |
|---|---:|
| IMU count / nominal 5 seconds | 100.000 Hz |
| IMU interval-derived mean rate | 100.0169 Hz |
| IMU spacing min / mean / max | 8833 / 9998.31 / 10782 us |
| IMU spacing standard deviation | 422.11 us |
| RMC count / nominal 5 seconds | 10.000 Hz |
| RMC interval-derived arrival rate | 9.96770 Hz |
| RMC arrival spacing min / max | 82726 / 118073 us |
| RMC UTC advancement | 50 epochs; every step exactly 100 ms |
| GGA count / nominal 5 seconds | 1.000 Hz |
| GGA interval-derived arrival rate | 1.000273 Hz |
| GGA arrival spacing min / max | 999213 / 1000719 us |
| GGA UTC advancement | 5 epochs; every step exactly 1000 ms |
| Maximum foreground flash service | 1108 us |
| Maximum store block persistence diagnostic | 369 ms |
| Maximum compression slice | 219 us |
| Maximum accumulated compression step time per block | 53859 us |

All per-stream timestamps strictly advance; the merged typed stream is
nondecreasing. IMU span is 4.989157 seconds across 499 intervals. GPS device
timestamps describe sentence reception, so arrival jitter is not UTC navigation
epoch jitter. The 200 us compression slice target is checked between work units;
the measured 219 us maximum includes the last unit's bounded overshoot. There
were no observed acquisition deadline misses.

Metadata-start to footer-stop timestamp is **5.002598 seconds**. Metadata start
slightly precedes the internal capture-start/deadline setup, and deadline service
occurs in the foreground. This is not a claim of exactly five seconds between
those metadata fields. Every recovered sample timestamp is within the nominal
five-second window from metadata start. Host START-command to observed STOPPED
was 6.703 seconds, including initial append scanning, USB and observation delay.
Clean completion was well before the 15-second deadline.

The 369 ms store diagnostic excludes gathering/compression/queue waiting and
does not measure individual flash tPP. Compression work_us also includes idle
update overhead and continues changing after capture; it is not pure codec CPU
time. Physical stack/heap high-water and actual CS/SCLK waveforms were not
instrumented in this run.

All 50 RMC observations have valid fixes, present date/time, advancing UTC and
position/speed. Course is present in 20 and absent in 30; masks 127/119 preserve
that absence without stale cached values. All five GGA records have quality=1,
UTC and quality fields; their associated dates remain explicitly marked as such.
No missing accuracy value or sensor sample was invented. Raw six-axis int16
measurements are retained. There is no separate physical input-byte trace to
compare against; integrity, duplicate exports and the separately tested lossless
codec support persistence/decoding, not a claim of an independent sensor oracle.

## Flash address map

The recovered committed pages occupy these exact ranges:

| Range | Content |
|---|---|
| `0x000000–0x0000FF` | Session header |
| `0x000100–0x000EFF` | 14 data pages |
| `0x001000–0x0010FF` | Continuation header |
| `0x001100–0x0016FF` | 6 data pages |
| `0x001F00–0x001FFF` | Clean footer |

23 valid programmed pages match firmware's **programs=23**. Two sectors occupy
8192 bytes; 5888 page bytes were programmed. Three compressed frames contain
1568, 1571 and 855 bytes (3994 total). The detailed per-page address list is in
`analysis.json`; the reconstructed two-sector image is also saved.

Maximum committed address is **0x001FFF**, within the authorized 0x00FFFF fence.
The reviewed engine and independent adapter enforce that fence; the full-header
read-only export found no additional sessions outside it. No program/erase API
available to this artifact can target the diagnostic sector. No erase command
was sent. There was no independent SPI bus trace or raw readback/hash of every
byte beyond 64 KiB or of the diagnostic sector; the outside-write conclusion is
based on the verified guards, observed program count and recovered addresses.

## Final state and recommendation

RECORD STOPPED, append position 0x2000; OBS OFF; COMP OFF, fault=0, queues empty;
TCB Idle with no errors. GPS remains RAM-profile verified with checksum_fail=0.
COM8 was released. The bounded build remains installed; it refuses another START
and will not record automatically. No erase, restart or longer recording is
scheduled.

**GO to prepare a separately reviewed bounded 30-second follow-up**, preserving
this first recording and appending after 0x2000. A proposed next build could
retain an absolute 256 KiB write ceiling and a 60-second total finalisation
deadline; those changes must be implemented, tested and separately authorized,
not assumed present now. Current firmware remains five-second/single-session:
**NO-GO for a longer recording with this artifact, unrestricted recording or
power-interruption testing.** This short real-input run does not replace
high-entropy sustained, voltage/temperature, endurance or brownout qualification.

Files: `first-write-1.tpx3`, `first-write-2.tpx3`, `decoded.json`,
`first-write.v2wire`, `recovered-recording-image.bin`, `analysis.json`,
`record-result.json`, `artifact-checks.json`, `preflight-result.json`,
`blank-before.tpx3`, startup/upload and serial transcripts in this directory.
