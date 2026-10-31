# First reusable-firmware USB-only hardware test — 31 October 2026

**PASS: one application upload and one ten-second recording.** Battery remained
disconnected by the user's physical confirmation; USB stayed connected throughout.
No external flash erase, bootloader modification, charge-current command, manual
GPS configuration or additional recording was performed. **COM8 is released.**

## Firmware and device

- Application: `tacticpod_reusable_usb`, TCB3 container **3.2.0**, corrected
  project-owned P0.14 LOW-before-OUTPUT variant. No firmware/configuration source
  was changed during this hardware test.
- Approved archive: `logs/switchless-battery-review/tacticpod_reusable_usb-switchless.zip`.
- Verified SHA-256:
  **ffee03178a49e2b77130da9afdefd5f5c358c6b22ecb3e1e249b2157ca540c67**.
- The reviewed source/configuration hashes and ten-suite validation evidence
  matched before upload. The earlier full 26-suite result remains archived.
- Device: **Seeed XIAO nRF52840 Sense**, USB **2886:8044**, serial
  **0E8A89B7289430BD**, application **COM8**. Existing DFU enumerated on **COM7**.
- One 1200-baud touch entered the existing loader; the proven nrfutil ZIP
  `dfu serial ... -p COM7 -b 115200 --singlebank` method returned **exit 0**,
  `Device programmed`. Only the application was supplied. No bootloader or
  SoftDevice image was in the package.
- Reusable duration/drain/lock markers were checked on the running application.
  No independent cryptographic readback of target internal firmware was obtained;
  the SHA is of the uploaded package, supported by successful DFU and runtime checks.

User confirmation before upload: **“LiPo disconnected; USB-only power; serial
monitor closed.”** This physical state was not inferred from ADC readings.

## Read-only preflight and preservation

Before upload, installed bounded profile3B10, stopped recorder states, GPS status,
raw JEDEC/status/SFDP and a TPX3 inventory were saved. Its reconstructed image was
byte-identical to the archived Phase3B10 image. After upload, another read-only
export confirmed the same image before START was allowed.

| Session | Sector allocation | IMU | RMC | GGA | Result |
| --- | --- | ---: | ---: | ---: | --- |
| 1 | 0x000000-0x001FFF | 500 | 50 | 5 | Preserved, complete |
| 3 | 0x002000-0x00AFFF | 3000 | 300 | 30 | Preserved, complete |
| 12 | 0x00B000-0x038FFF | 12001 | 1200 | 120 | Preserved, complete |
| **58, new** | **0x039000-0x03BFFF** | **1000** | **100** | **10** | **Complete, clean** |

The entire previously exported 0x000000-0x038FFF prefix is byte-identical before
and after the test. Prefix SHA-256:
`b9ad38d83ac27a61a79bf58cbbba68cc44a03d6dddeebcf287d9166eb76fb6f6`.
All four sessions independently decoded successfully. No old session was resumed.

## Startup, GPS, IMU and flash

Normal boot/read-only recovery completed. No IMU initialisation error occurred;
explicit IMU ON preview produced live six-axis values and IMU OFF was restored.
Subsequent binary data verifies the recording rate separately from that 10Hz preview.

Startup GPS result:

```text
GPS PROFILE: STARTUP 10HZ ALREADY VERIFIED (no writes; original snapshot unavailable)
First mismatch before recovery: NONE
GPS PROFILE END | UART=38400 | snapshot=none | trace_dropped=0 ignored_ack_nak=0 | no BBR/flash/OTP writes; recordings unchanged.
```

GPS STATUS verified MEAS100ms/NAV1, GPS+Galileo enabled, BeiDou disabled,
RMC every epoch, GGA every tenth epoch, GSA/GSV/GLL/VTG disabled, UART38400.
There was no original rollback snapshot because the receiver already matched the
profile; this was reported rather than invented. No TEST/RESTORE command was sent.

A fresh 15-second GPS RATE diagnostic measured **150 RMC, 15 GGA**, all valid fixes
with unique advancing UTC epochs, zero checksum failures/overlong frames, primary
RMC and approximately-10Hz cadence YES. During the new recorded session, all
**100 RMC fixes were valid**, with **100 unique UTC epochs exactly100ms apart**;
GGA had ten unique UTC epochs exactly1000ms apart, quality1. Missing optional RMC
fields remain explicit in masks (0x77/0x7F); they were not filled from stale values.

Flash pre/post-upload identity reads matched the archived fingerprint:
**JEDEC0B4018**, SR1 before/after **00**, SR2 **02**, repeated JEDEC and both SFDP
passes **PASS**. Offline SFDP density decoding yielded **16,777,216 bytes**,
**128Mbit /16MiB**, not 128MB. This confirms the established B-compatible
fingerprint, not an exact independently identified manufacturer/model revision.

## Ten-second recording and timing

Only these recording-control commands were sent:

```text
TCB DURATION 10
TCB START
```

STARTED reported session58. USB remained connected; no manual STOP was sent.
Automatic capture/finalisation completed cleanly. Host observed finalisation at
approximately **10.547 seconds** after STARTED, using roughly0.5-second polling;
this is not exact footer-program latency.

| Measurement | Physical result |
| --- | --- |
| Header start to footer stop | **10.002992s** |
| IMU count / timestamp rate | **1000 /100.0059Hz** |
| IMU timestamp spacing | **9270-10926us**, mean9999.407us |
| RMC count / timestamp rate | **100 /10.0055Hz** |
| RMC arrival timestamp spacing | **86612-115886us**, mean99945.121us |
| RMC UTC epoch spacing | **100ms**, no duplicates/backwards/missing epochs |
| GGA count / timestamp rate | **10 /1.00003Hz** |
| GGA arrival timestamp spacing | **998987-1000881us** |
| Offered / committed observations | **1110 /1110** |
| Lost IMU/RMC/GGA/EVENT | **0 /0 /0 /0** |
| Rejected / missed / full / CRC / incomplete | **All zero** |
| Compressed blocks | **5** |
| Max flash service call | **1001us** |
| Max block persistence | **369ms** |
| Max compressor processing slice | **298us** |
| Max aggregate compressor block work | **53605us**, distributed over slices |
| Compressor queue high-water | Input2/2, output1/2 |
| Backpressure slices / raw fallbacks | **0 /0** |

All device timestamps were preserved and globally nondecreasing; each stream's
timestamps were unique. GPS arrival jitter is distinct from the exact UTC epoch
cadence. The footer recorded reasonClean and zero loss/error counters. No unexpected
reset, assert, brownout indication or flash fault was observed during recording.

The only new occupied pages are listed in metrics.json: **43 single-program
pages**, from 0x039000 through the footer page at **0x03BF00-0x03BFFF**. They occupy
three sectors (**12288 allocated bytes**, **11008 programmed page bytes**).
Continuation-sector footer slots and unused data pages remain blank in the export.
The page inventory matches the runtime43 program count. This is readback/runtime
evidence, not an independent SPI bus trace. The diagnostic sector is excluded by
the unchanged hard fence and no command targeted it; no raw diagnostic-sector
hash was captured during this run.

Next append: **0x03C000**, next address-derived session **61**. Do not reuse the
old session58/0x039000 expectation for another test.

## Binary export and integrity

Files **after-1.tpx3** and **after-2.tpx3** are exact raw binary captures, each
**268864 bytes**, with identical SHA-256:

**92708b9d73289781ba296fde11afe6498d433dc34aa2f3ff75ebae95f28ff6ac**

TPX3 packet CRC/sequence checks, page CRC/commit seals, fragment/block continuity
and CRC, sector/page/block sequences, metadata, timestamp order, capture duration
and footer accounting passed. Each old/new session was decoded independently;
the complete previous prefix is unchanged. Session JSON files retain decoded
full-resolution values and validity fields. Export was read-only.

Key evidence: before-upload.tpx3, after-upload-before-recording.tpx3,
after-1.tpx3, after-2.tpx3, before-upload-image.bin, after-image.bin,
session-1.json, session-3.json, session-12.json, session-58.json,
metrics.json and RESULT.json. Raw transcripts and upload output are preserved.

## Warnings and host-check interruptions

1. GPS showed **one cumulative checksum failure after boot**, before recording.
   It did not increase during fresh RATE measurement or through recording/export.
   The RATE observation window itself had zero failures. Cause of the earlier
   failure is not established; it is not hidden or counted as a storage CRC fault.
2. The first post-upload host assertion expected `profile_error=none`, a field
   this firmware does not print. Actual GPS status was correct. Read-only checks
   resumed with the documented output; upload was not repeated.
3. Immediately after clean TCB finalisation the firmware intentionally refreshed
   the legacy index with a read-only scan. An immediate host assertion expected
   STOPPED and saw SCANNING. It waited for normal completion before exporting;
   no second START, reset, erase or extra recording occurred. Both partial host
   result files are retained; **RESULT.json/verification-result.json** describe
   the final successful outcome.
4. Before upload, the old bounded build retained OBS diagnostic overflows
   RMC191/GGA15 while OBS was OFF, as previously documented. Its TCB footer loss
   counters were zero and archived recordings matched exactly. New reusable OBS
   counters were zero with capture OFF; no new recording loss was observed.
5. BAT STATUS remained **ADC_UNVALIDATED, cell0/calibrated0/trusted0, VBUS1**,
   with no ADC timeout or battery warning/stop. Plausible nominal voltages on
   USB do not measure a connected cell; no calibration/charging setting changed.

## Final state and recommendation

RECORD STOPPED, OBS OFF, COMP OFF/drained, TCB Idle/session58/duration10/drain60,
lock0/errornone. IMU printing OFF. GPS continues receiving at its verified RAM
profile. COM8 was closed/released after verification. Reusable firmware remains
installed. No further test is automatically scheduled or authorised.

**GO for continued controlled USB-only use. NO-GO for the subsequent battery-
powered test yet:** this successful USB run does not eliminate P0.14/P0.31
reset/bootloader exposure, qualify cell polarity/charging/current/thermal
behaviour, or test physical power handover. Battery remains disconnected.
Those existing electrical risks need resolution/review before separate battery
connection approval; no hardware change or bootloader modification was performed.
