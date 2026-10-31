# GPS UART reception

This GPS build is **not uploaded**. The user reports a hardware scan at 38400
baud with 1929 bytes, 42 valid NMEA sentences and no framing errors in three
seconds. Normal XIAO reception now matches that confirmed rate. This new build's
operation and timing still require hardware validation after approved upload.

## Verified XIAO pins

The installed Arduino BSP's `variants/Seeed_XIAO_nRF52840/variant.h` defines
`PIN_SERIAL1_RX = 7` and `PIN_SERIAL1_TX = 6`. Its `variant.cpp` maps D7 to
P1.12 and D6 to P1.11. `cores/nRF5/Uart.cpp` constructs hardware `Serial1`
on UARTE0 using those exact variant definitions. The GPS module has a compile
time assertion to prevent silently building against a different UART mapping.

| GPS breakout | XIAO connection |
| --- | --- |
| V | 3V3, as requested for testing |
| G | GND |
| T (TX) | D7 / P1.12, hardware UART RX |
| R (RX) | D6 / P1.11, hardware UART TX |
| PPS | Unconnected |

`Serial1.begin(38400)` uses the core's default 8 data bits, no parity, one stop
bit. USB serial remains `Serial` at 115200 baud. The
[u-blox MAX-M10S integration manual](https://content.u-blox.com/sites/default/files/MAX-M10S_IntegrationManual_UBX-20053088.pdf)
documents a MAX-M10S 9600-baud configuration; this project's normal rate instead
matches the user's measured 38400-baud output. Startup automatically applies
the verified RAM-only 10Hz profile after valid communication and idle flash/
recording diagnostics; see [startup, restore and power-cycle behaviour](gps-10hz.md).
`GPS CONFIG` explicitly transmits
read-only UBX polls. `GPS 10HZ TEST` and `GPS RESTORE` explicitly change RAM-only
settings with snapshot/verification safeguards; other normal GPS commands remain
receive-only. The isolated loopback test transmits its fixed test pattern.

## Commands

| Command | Action |
| --- | --- |
| `GPS STATUS` | Queue one readable GPS status report. |
| `GPS ON` | Enable a readable report approximately once per second. |
| `GPS OFF` | Stop GPS printing; reception and parsing continue. |
| `GPS RAW` | Capture incoming UART bytes for 10 seconds in RAM, then display escaped raw data and a summary. |
| `GPS RATE` | Measure separate checksum-valid GGA/RMC message and UTC epoch rates for 15 seconds, even without a fix. |
| `GPS CONFIG` | While stopped, read current settings and stored clock verification using checksum-validated UBX polls. |
| `GPS 10HZ TEST` | While stopped, snapshot SPG 5.10 settings and apply a verified, reversible RAM-only GPS+Galileo 10 Hz profile. |
| `GPS RESTORE` | While stopped, restore all saved profile values and verify readback. |
| `GPS SCAN` | While stopped, measure six receive rates for three seconds each and restore 38400. |
| `UART LOOPBACK TEST` | Temporary explicit UART TX/RX test with GPS UART disconnected and D6 bridged to D7. See the investigation below. |

See [UART investigation and physical loopback procedure](uart-investigation.md)
for the audit of the installed BSP and the required GPS-disconnected fixture.
The separate [receive-only baud scan](gps-scan.md) uses normal GPS wiring,
without the loopback jumper, and reports actual UART registers and error flags.
See [GPS RATE and measured current output/bandwidth](gps-rate.md) for the
15-second observer, no-fix behavior, and current 1 Hz baseline measurement.
See [read-only configuration queries and limitations](gps-config.md) for
GPS CONFIG's RAM/OTP layers, unsupported keys, interlocks and 10 Hz assessment.
See [10 Hz test profile and rollback limits](gps-10hz.md) for exact settings,
RAM snapshot lifetime, automatic recovery and expected UART bandwidth.

Printing is OFF at boot. No GPS errors are printed repeatedly while OFF;
use `GPS STATUS` or `GPS ON` to inspect communication errors. There is a
five-second initial waiting/freshness window. With no received bytes, status
reports a UART-data error after that window, including RX wiring, power and
baud checks. Bytes without checksum-valid NMEA are distinguished from a silent
UART. Loss of a previously working UART is also reported.

Status includes:

- Whether recent checksum-valid NMEA is arriving, byte/checksum counters, and
  milliseconds since the last checksum-valid sentence (or N/A).
- Latitude/longitude in decimal degrees and whether the position is current
  or merely cached/unavailable; cached coordinates are never presented as a
  current fix after an explicit no-fix sentence or stale data.
- Fix VALID, NO FIX, UNKNOWN, or STALE/UNKNOWN. This reflects NMEA validity,
  not an independently determined 2D/3D fix classification.
- Satellites **used** as reported by GGA, HDOP when present and fresh, speed
  in km/h, UTC time as HH:MM:SSZ, and cached-position age.

Valid serial NMEA does not imply a satellite fix. GNSS time may be available
without a position fix. Speed is not reported as current unless the fix and
speed are fresh. Data older than five seconds is treated as stale. An empty
GGA HDOP/satellite field is displayed as N/A instead of a reused old value.

## Non-destructive raw UART capture

Send `GPS RAW` followed by Enter. It uses the same verified hardware UART at
38400 baud on D7 RX / D6 TX, and sends nothing to the GPS. It does not access or
modify flash. Any recording already running continues normally.

There is exactly one UART reader. Its capture tap sees each byte **before**
TinyGPSPlus, copies it to RAM during the 10-second window, and still feeds the
normal parser. NULs, high-bit bytes, CR/LF and other controls all contribute to
the byte count. A separate parser, reset at capture start, counts checksum-valid
NMEA sentences wholly observed within the window; an old partially received
sentence cannot incorrectly establish valid NMEA for the capture.

Raw data is displayed **after the capture finishes**, rather than streamed while
sampling. Printable characters remain readable, including normal `$GNGGA` and
`$GNRMC` sentences. All non-printable bytes use `\xHH` notation (including
`\x00`, `\x1B`, `\x0D`, `\x0A`); literal backslashes are escaped as `\\`.
Terminal control bytes are never emitted directly. Normal NMEA lines are kept
together; long/binary data is split into bounded lines prefixed `GPS RAW | `.

The final line is:

```text
GPS RAW END | duration_ms=10000 | total_bytes=... | stored_bytes=... | omitted_bytes=... | valid_nmea=YES/NO | valid_sentences=...
```

With zero bytes it also suggests checking TX-to-D7, power/GND and baud. The
49,152-byte (48 KiB) RAM buffer exceeds the approximately 38,400 bytes possible
over ten seconds at 38400 baud / 8N1. If unusually large input fills it, counting and
checksum validation continue; `omitted_bytes` explicitly reports bytes that
could not be retained for display. Counts describe bytes delivered by the
hardware UART driver, rather than electrical transitions on the RX pin.

Capture and display are cooperative: each loop still reads at most 64 UART
bytes, there are no waiting delays, and output is written only as complete
lines that fit available USB TX space. USB backpressure/disconnection does not
stop capture or IMU acquisition; captured data is retained for later display.
Do not issue another `GPS RAW` until `GPS RAW END`; duplicates are refused and
never restart the timer. `GPS STATUS` is refused while RAW is pending, and
`GPS OFF` disables regular reports without cancelling the raw capture. Regular
one-second GPS output pauses until the raw report ends, then resumes if enabled.

A new raw capture is refused during CSV export. If an export starts during an
existing capture, reception/capture continues but raw output waits until export
ends, preserving the CSV stream. No raw bytes are written to flash and the
binary IMU format is unchanged.

## Parser and scheduling

The project vendors the unmodified official TinyGPSPlus tag `v1.0.3a`, commit
`ca29434514a5c5172bd807af0608df7f296582a2`, under `lib/TinyGPSPlus`.
PlatformIO discovers it automatically. The parser supports GP and GN GGA/RMC
sentences, including the multi-constellation GN talker used by modern u-blox
receivers. Custom checksum-validated GGA/RMC listeners detect loss of fix,
since TinyGPSPlus intentionally retains previously valid positions.

Every main-loop iteration processes at most 64 UART bytes and never waits for
another character or for a whole sentence. The hardware UART continues to
receive via interrupts. Reports are formatted into a fixed RAM buffer and
drained one complete line at a time only when USB `availableForWrite()` has
enough space. GPS reception/status/RAW have no delays, wait-for-fix loops or
UART writes. Only the explicit temporary loopback command transmits its fixed
test pattern, with GPS UART disconnected; it sends no receiver configuration.
The existing 100 Hz IMU sampling, flash writer, LED timer and serial command
handler continue to run.

GPS reception continues with USB disconnected and during CSV export.
Background GPS printing is suppressed during `RECORD READ`; `GPS STATUS`
is refused during that export. Avoid issuing commands mid-export, since
explicit command acknowledgements share USB serial with CSV. The existing
destructive-test command gate still ignores commands while that test is
active or faulted.

GPS data is **not added to recordings**. `RecordFormat::Sample` remains
16 bytes, and all session/page headers, CRCs, commit markers and flash
addresses are unchanged.

## Validation

Baud-dependent assumptions updated for this build:

- Normal UART and scan restoration now share `gps_uart_config.h` at 38400.
  The actual UARTE baud-register expectation is `0x009D0000`.
- Scan output changes from `restored_9600` to `restored_38400`; any host script
  matching that field must follow the new label. The six scan rates are unchanged.
- RAW RAM increases from 12 KiB to 48 KiB to retain ten seconds at full line
  rate. Total firmware static RAM is 65,148 bytes (27.4%). Test-only USB sink
  capacity was also enlarged to validate the longer escaped dump.
- The core RX ring remains 64 bytes: at maximum 38400-baud 8N1 traffic it fills
  in roughly 16.7 ms rather than 66.7 ms at 9600. Bounded UART polling remains
  in every main loop. New simulations exercise full-rate input alongside
  100 Hz IMU recording; real interrupt/IO timing is not modeled by simulation.
- Loopback uses the new normal rate. Its 32-byte payload and one-second timeout
  are unchanged; `TACTICPOD_UART_9600` inside the payload is a legacy test string,
  not a rate setting. Transmission takes roughly 8.3 ms at 38400.
- NMEA parsing, one-second readable output, ten-second RAW timing, per-rate
  scan timing, binary recording layout and all flash protections are unchanged.

All 86 prior cases pass with new normal-rate expectations, plus two new cases
for a full 38,400-byte RAW capture and full-rate reception with 100 Hz recording.
No test reconfigures the GPS or accesses the physical flash.

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" tests/flash_write/run_tests.py
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" tests/recording/run_tests.py
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" tests/gps/run_tests.py
```

The GPS tests execute the actual GPS module and TinyGPSPlus on simulated ARM
with mock UART/USB/flash. They check GP/GN parsing and numeric fields, checksum
rejection, explicit fix loss, stale data, missing fields, UART errors, output
rate, GPS OFF reception, USB backpressure, incomplete sentences, CSV protection,
and 100 Hz IMU recording alongside incoming GPS bytes. No tests touch hardware.
Raw-capture tests additionally cover the duration/end boundary, NUL/binary byte
counts, safe escaping, checksum acceptance/rejection, pre-capture fragments,
duplicate commands, USB backpressure/disconnection, CSV protection, buffer
overflow reporting, timer wraparound, and a ten-second capture while recording
1000 IMU samples without simulated missed deadlines or lost samples.
