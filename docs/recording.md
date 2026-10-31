# Binary IMU recording

This build is not uploaded. Hardware recording speed, timing and power-cut
behavior still need validation after upload is approved. Simulation exercises
the actual ARM recorder code, but does not model real flash electrical failures.

## Commands

Use USB serial at 115200 baud. Finish exact commands with Enter.

| Command | Behavior |
| --- | --- |
| `DIAG` | Display JEDEC/SFDP information once while flash is idle. Refused while scanning, recording, exporting, erasing or faulted. |
| `RECORD START` | Start a new 100 Hz session in verified blank space after existing sectors. Never erases. |
| `RECORD STOP` | Stop acquisition, drain the RAM queue, commit a partial page and a session footer. Wait for `RECORD STOPPED`. |
| `RECORD STATUS` | Report state, current-session committed/buffered samples, overflow loss, missed deadlines and recovered append position. |
| `RECORD READ` | While stopped, export all supported, valid committed sample pages as CSV. |
| `RECORD ERASE` | Display a warning and arm a separate erase confirmation for 30 seconds. Does not itself erase. |
| `RECORD ERASE CONFIRM` | After the warning, explicitly erase the recording area sector by sector. Never touches the diagnostic sector. Recording remains OFF afterward. |

Wait for `RECORD scan complete` after boot before starting. Startup scans flash
read-only; it never resumes an old session or erases anything. `RECORD START`
appends a new session after the highest occupied sector header, so existing
sessions are preserved. If there is no room or an apparently unused sector
contains old payload, recording refuses to overwrite it. Export existing data
before choosing an explicit recording-area erase. An interrupted erase may
leave usable blank space below occupied sectors; conservative recovery will
not reuse those holes automatically.

JEDEC/SFDP diagnostics print automatically only once per boot, after the scan
finishes and USB serial is connected. If the scan completed before the serial
monitor opened, its completion notice is retained and displayed when connected.
After startup, there is no periodic diagnostic or default IMU output. Send
`DIAG` for another report, or `IMU ON` to explicitly enable live IMU printing.
The connection menu and destructive-test warning remain available on reconnect.

`RECORD READ` produces one CSV stream between `BEGIN_RECORD_CSV` and
`END_RECORD_CSV`:

```csv
session,timestamp_ms,accel_x_g,accel_y_g,accel_z_g,gyro_x_dps,gyro_y_dps,gyro_z_dps
```

Each session number is its first sector index plus one. Session IDs are unique
until the recording area is explicitly erased; after erase they may repeat.
Timestamps are unsigned 32-bit `millis()` values from the recording boot, not
wall-clock dates; they wrap after approximately 49.7 days. Sessions from separate
boots have separate timestamp origins. CSV converts raw signed sensor values
using the recorded format's fixed sensitivities. Values are not bias-calibrated.
Export rejects unknown versions/scales, invalid CRCs and missing commit markers.
At the first invalid sample page in a sector, it stops exporting that sector's
tail and continues scanning later sector headers. Missing or torn footers do
not prevent recovery of earlier committed pages.

JEDEC/SFDP diagnostics and live IMU printing are suppressed during CSV export.
Avoid sending additional commands mid-export, since command responses also use
USB serial. Export is incremental, stops on serial disconnect, and sends no
flash-modifying commands. `RECORD STOP` stops recording, not an explicitly
confirmed area erase; let an erase complete before starting another command.

## Acquisition and buffering

Idle acquisition stays at 10 Hz. During recording, the main loop targets one
sample every 10 ms regardless of USB connection or `IMU ON/OFF`. The sensor's
accelerometer and gyro remain enabled at their supported 104 Hz ODR, +/-2 g
and the library's 245 dps range. Six raw axes are read once per sample;
live output uses the same values but remains limited to 10 Hz.

A 256-sample ring buffer uses 4096 bytes of RAM. Pages hold 14 samples, and
one 256-byte page body is programmed approximately every 140 ms. A separate
four-byte commit marker is programmed last, after body readback verification.
Status/WEL/WIP checks guard each operation, followed by readback and CRC checks.
The state machine polls at 2 ms intervals without waiting in a busy loop;
program timeout is 1 second and sector erase timeout is 5 seconds.

`missed_deadlines` counts acquisition intervals missed by the main loop;
it does not fabricate replacement samples. USB printing, I2C/SPI execution
and interrupts can affect real-device timing. On RAM overflow, acquisition
stops, retained samples are flushed, and dropped samples are reported. On
area exhaustion, queued samples that cannot fit are counted as lost; committed
data is preserved. `committed_samples` is for the current boot's latest session,
not a historical total recomputed from all pages at startup.

## Flash layout and recovery

| Area | Addresses |
| --- | --- |
| Recording area: 4095 x 4 KB sectors | `0x000000`-`0xFFEFFF` |
| Existing diagnostic test sector | `0xFFF000`-`0xFFFFFF` |

Each recording sector reserves page 0 for a session/continuation header,
pages 1-14 for sample data, and page 15 for an optional clean-stop footer.
A session continues across sectors. Starting a new session always consumes a
fresh sector; unused space in an old sector is not recycled. Continuous
recording has a maximum of 802,620 samples, roughly 134 minutes at 100 Hz,
with lower capacity for many short sessions.

All integers are serialized little-endian. Every 256-byte page has:

| Offset | Meaning |
| --- | --- |
| 0 | 32-bit magic: `TPH1` header, `TPD1` data, `TPE1` footer |
| 4 | 32-bit session ID |
| 8 | Global sample-page sequence within the session |
| 12 | Data: sample count 1-14; header: sector sequence; footer: committed sample count |
| 16-247 | Payload, unused bytes padded `0xFF` |
| 248 | CRC-32/IEEE of bytes 0-247, reflected polynomial `0xEDB88320`, initial/final XOR `0xFFFFFFFF` |
| 252 | Commit marker `0xC0DEC0DE`, programmed after the body verifies |

Header payload contains version 1 at offset 16, sample rate 100 at 20,
acceleration sensitivity 61 micro-g/LSB at 24, gyro sensitivity 8750
micro-degrees/second/LSB at 28, and session-start milliseconds at 32.
Data payload contains up to 14 samples, each 16 bytes: uint32 milliseconds,
then int16 accel X/Y/Z and gyro X/Y/Z. Footer payload records overflow/full
loss count at 16 and missed-deadline count at 20.

Before a new sector header is written, every byte in that sector must be
`0xFF`. Before each page program, the entire destination page is checked again.
Recovery treats any nonblank sector-header prefix as occupied, even if torn;
a blank prefix is only a candidate and is never trusted without the full-sector
check. Writes never cross a page boundary, and recording erase/program helpers
reject addresses reaching `0xFFF000`. There is no full-chip erase command.
The diagnostic `ERASE FFF000` is refused while recording, scanning, exporting,
erasing, or faulted. Recorder operations are refused during the diagnostic test.

After a power interruption, only pages with valid markers and CRCs are
exported. In-flight pages and samples still in RAM may be lost; this is not a
zero-loss power-fail logger. No old session is resumed or overwritten. If flash
is still busy at boot it is polled with a timeout before the read-only scan.
An uncertain response, timeout, denied WEL, or failed readback locks all flash
activity until reboot. IMU acquisition and LED service continue. This code
does not change protection registers or attempt to unlock protected storage.

## Validation

Run from the project directory:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" tests/flash_write/run_tests.py
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" tests/recording/run_tests.py
```

The test runners use the installed ARM compiler and project-local Unicorn and
pyelftools packages in `.pio-core/test-python`. They run against mock NOR and
serial devices, with no hardware access. Tests cover original diagnostic safety,
recording across pages/sectors, exact sample decoding, append/reboot behavior,
CSV export, partial pages, empty sessions, torn markers/headers, payload CRC
failure, old payload under a blank header, timeouts, wrong JEDEC, denied WEL,
readback failure, overflow/full-area behavior, erase confirmation/expiration,
reserved-sector boundaries, and 100 Hz main-loop acquisition with USB disconnected.
Additional tests cover quiet idle output, one-shot startup/manual diagnostics,
scan-complete delivery after a late USB connection, and DIAG safety interlocks.
