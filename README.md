# TacticPod_Prototype

Arduino / PlatformIO IMU test for the original Seeed Studio XIAO nRF52840 Sense.

## Current build: automatic RAM-only GPS 10Hz startup

After checksum-valid GPS communication and idle flash/recording diagnostics,
startup automatically runs the tested GPS 10HZ TEST profile with its corrected
transaction sequence, original snapshot, ACK validation and full readback.
Read timeouts have two bounded retries; write failures use verified rollback.
Startup finishes without a USB host, while IMU acquisition continues.
GPS STATUS reports the last verified profile and any configuration error.

The previous three-packet profile placed rate/message values in APPLY, where
SPG 5.10 ignores them. The corrected sequence is START, CONTINUE, CONTINUE,
empty APPLY for both TEST and RESTORE. Correcting the simulated receiver to
match section 4.6 reproduces MEAS remaining 1000 ms with the original sequence.
The U2 encoding of 100 ms was already correct (`64 00`).

Diagnostics now preserve requested/actual readbacks before rollback, separate
TEST and RESTORE values, and every transaction stage's ACK/NAK/timeout status.
The 600 ms settling margin and strict verification remain unchanged. See the
[failure investigation and exact wire format](docs/gps-10hz-investigation.md).

Adds explicit, stopped-only commands for the MAX-M10S advertising SPG 5.10.
TEST captures 22 profile values and seven guards before any write, then uses
RAM-only UBX-CFG-VALSET transactions for GPS+Galileo with QZSS support, 100 ms
measurement / NAV=1, RMC every epoch and GGA every tenth epoch. UART1 GSA, GSV,
GLL and VTG are disabled. UART stays at 38400, protocol gates and time reference
are preserved, and no CPU/OTP/BBR/flash settings are changed.

Every profile value and guard is read back. RESTORE replays the original saved
profile and verifies it. Failures automatically attempt rollback; an unverified
rollback retains the original snapshot for retry. The snapshot is in XIAO RAM
only and is lost on reset/reflash, so an independently powered GPS can outlive
it. RESTORE leaves the original settings active for the rest of that boot;
the next board startup reapplies 10Hz. A GPS-only power cycle reloads its own
persistent/default settings and does not restart the board's startup job.
Both explicit commands reject recording/busy states.
See [exact key table, bandwidth and rollback limits](docs/gps-10hz.md).

**Built, not uploaded or executed on hardware.** Build passed without reported
compiler/linker errors or warnings. All 186 simulated tests pass (13 flash,
20 recording, 153 GPS/UART), including eight startup cases and the existing
profile/rollback cases. Existing 100 Hz IMU recording, storage
format/layout, LED, flash protections and GPS diagnostics are preserved.

## Previous addition: read-only GPS CONFIG

Adds stopped-only `GPS CONFIG`: checksum-validated UBX-CFG-VALGET polls for
constellations/signals, measurement/navigation rates, time reference, UART1
protocol/message output and navigation/power settings. UBX-MON-VER reads receiver
identity; separate read-only OTP queries verify the stored high-clock profile.
No receiver settings or flash recordings are changed. UART remains 38400 on
D7 RX / D6 TX, and IMU recording, commands and LED behavior are preserved.

**Built, not uploaded.** Build passed without reported compiler/linker errors
or warnings: RAM 67,424 bytes (28.4%), flash 111,556 bytes (13.8%). All 134
simulated tests pass (13 flash, 20 recording, 101 GPS/UART), including 28 new
configuration diagnostic cases. Actual receiver values and supported keys await
hardware verification after approved upload. The command distinguishes unknown
values, NAKs and timeouts; it does not claim a stored clock profile proves live
10 Hz navigation. See [usage, official sources and limitations](docs/gps-config.md).

## Previous addition: non-destructive GPS RATE

Adds `GPS RATE`: a 15-second checksum-valid GGA/RMC observer, with separate
message/UTC epoch rates, duplicate detection and fix/no-fix counts. Works indoors
and alongside 100 Hz IMU recording. UART remains 38400 and no receiver settings,
flash data or recording format are changed. **Not uploaded.**

The existing installed RAW diagnostic was used to sample current output without
uploading: 10 GGA and 10 RMC messages in 10 seconds, each with 10 distinct UTC
epochs and no valid fixes. Current message output is **1 Hz**. 38400-baud 8N1
has 3840 bytes/s payload capacity, enough for a restrained GGA/RMC set at 10 Hz;
scaling the entire observed 570 bytes/s stream by ten would exceed capacity.
See [measurements, limits and GPS RATE usage](docs/gps-rate.md).

Build passed without reported compiler/linker errors or warnings: RAM 65,728
bytes (27.7%), flash 101,984 bytes (12.6%). All 106 simulated tests pass
(13 flash, 20 recording, 73 GPS/UART). COM8 was released after the read-only
baseline capture; no recording or destructive operation was started.

## Normal GPS at 38400 baud

Updated only XIAO reception to match the user's measured GPS output: 1929 bytes,
42 valid NMEA sentences and zero framing errors in three seconds at 38400.
`GPS STATUS`, `GPS RAW`, scan restoration and loopback use/report 38400. The
GPS module itself receives no configuration changes. Six scan rates, recording
format/layout, IMU, LED and flash protections remain unchanged.

RAW buffering increases to 48 KiB for ten seconds at the higher rate. Scan's
result field is now `restored_38400`. See [affected assumptions](docs/gps.md).
Build passed without reported compiler/linker errors or warnings: RAM 65,148
bytes (27.4%), flash 96,904 bytes (11.9%). All 88 simulated tests pass, including
every prior case plus full-rate RAW/recording tests. **Not uploaded.** No flash
data or recordings were modified.

## Receive-only GPS SCAN

Adds `GPS SCAN`, accepted only while recording/flash/UART diagnostics are idle.
It measures UART input for three seconds at each of 4800, 9600, 19200, 38400,
57600 and 115200 baud, reports bytes, NMEA and hardware error flags, and checks
actual peripheral RX selection and receive state. It restores 9600 before
printing the final report. No GPS transmissions, receiver configuration,
flash writes or automatic recording are added.

[Instructions and error-counter limitations](docs/gps-scan.md).
Built and **not uploaded**. No reported compiler/linker errors or warnings;
RAM 28,284 bytes (11.9%), flash 96,920 bytes (12.0%). All 86 simulations pass
(13 flash, 20 recording, 53 GPS/UART); physical scan remains untested.

## UART investigation and loopback

The installed board/BSP was audited before editing: Serial1 is hardware UARTE0
on D7 RX/P1.12 and D6 TX/P1.11 at 9600 baud. No UART reinitialisation, pin
conflict or GPS RX discard path was found. Adds explicit `UART LOOPBACK TEST`
with a 32-byte pattern, timed receive/compare, byte counts and PASS/FAIL.

**Disconnect GPS TX/RX before bridging D6 to D7 for the test.** Nothing runs
automatically. GPS settings, flash contents/layout and recording logic are
unchanged. [Investigation and test procedure](docs/uart-investigation.md).

Built, **not uploaded**. No reported compiler/linker errors or warnings;
RAM 27,248 bytes (11.5%), flash 90,580 bytes (11.2%). All 70 simulated cases
pass (13 flash, 20 recording, 37 GPS/UART). Physical loopback remains untested.

## Non-destructive GPS RAW diagnostic

Built and **not uploaded**. Adds `GPS RAW`: a nonblocking ten-second UART
capture in RAM, before the normal GPS parser. All bytes are counted; printable
NMEA stays readable, controls/binary use safe `\xHH` escapes, and a final report
shows byte totals, omitted bytes and checksum-valid NMEA count. Display follows
capture and waits for USB TX space. GPS settings, SPI wiring, recording format
and flash safety logic remain unchanged; RAW sends no GPS configuration or
flash writes. See [raw UART capture instructions](docs/gps.md).

Build passed without reported compiler/linker errors or warnings: RAM 27,208
bytes (11.5%); flash 87,504 bytes (10.8%). All 58 simulated cases pass
(13 flash, 20 recording, 25 GPS). Hardware capture awaits approved upload.

## MAX-M10S GPS reception

Built and **not uploaded**. Adds hardware `Serial1` GPS reception at 9600 baud
and `GPS ON`, `GPS OFF`, `GPS STATUS`. The installed XIAO board variant was
checked: RX is D7/P1.12 and TX is D6/P1.11. GPS output is OFF by default;
reception continues regardless. Receiver configuration and update rate are
untouched. TinyGPSPlus is vendored at official tag `v1.0.3a`.

All IMU, recording, flash, diagnostic and LED behavior remains; no GPS fields
are added to the existing binary recording format. See
[GPS wiring, status fields and validation](docs/gps.md).
Build passed without reported compiler/linker errors or warnings: RAM 14,676
bytes (6.2%); flash 84,584 bytes (10.4%). All 45 simulated tests pass (13 flash,
20 recording, 12 GPS). Real GPS communication and timing remain unverified.

## Quiet Serial Monitor

Built and **not uploaded**. JEDEC/SFDP diagnostics now print once per boot,
after read-only recovery completes and USB serial is connected. `DIAG` requests
another read-only report while flash is idle; it is refused while busy/faulted.
The `RECORD scan complete` notice is retained if scanning finishes before the
monitor opens. After startup there are no periodic reports or default IMU
readings. The menu and safety warning still appear on serial connection.

Recording and IMU printing remain OFF by default. Recording commands, SPI
wiring, recording layout and flash protections are unchanged. Final build:
RAM 13,052 bytes (5.5%); flash 72,528 bytes (8.9%); no reported compiler/linker
errors or warnings. All 33 simulated cases pass (20 recording, 13 regressions).

## 100 Hz binary recording

Built, simulated and **not uploaded**. Adds `RECORD START`, `RECORD STOP`,
`RECORD STATUS`, and `RECORD READ` (CSV export). Recording is OFF at startup.
Existing IMU printing commands, LED, JEDEC/SFDP reports and the diagnostic
test are preserved, with flash operations mutually excluded while busy.

The recording area is `0x000000-0xFFEFFF`; the final 4 KB diagnostic sector
remains reserved. Recording appends only to verified blank space using buffered
16-byte samples, 256-byte pages, session headers, CRCs and commit markers.
No startup erase occurs. Reusing occupied space requires the separate
`RECORD ERASE` warning followed by `RECORD ERASE CONFIRM` within 30 seconds.

See [recording commands, format, recovery and tests](docs/recording.md).
Final build: RAM 13,052 bytes (5.5%); flash 72,280 bytes (8.9%).
All 30 simulation cases pass (18 recording cases plus 12 existing regressions).
The sections below describe earlier builds.

## Serial Monitor controls

Built successfully; **not uploaded**. The IMU now reads all six axes every
100 ms and retains the latest sample in RAM even with printing disabled or
USB serial disconnected. Printing defaults to OFF on each boot. This does
not add recording to flash or a sample history buffer.

A simple menu appears when the serial monitor first connects and on reconnect:

```text
IMU ON       - print IMU readings at 10 Hz
IMU OFF      - stop printing; continue sampling at 10 Hz
ERASE FFF000 - existing explicit confirmation for ONE destructive flash test
```

Send each exact command followed by Enter (CR, LF or CRLF). The existing
parser still ignores input while a flash test is active or flash commands
are locked. IMU initialisation errors print once per connection even when
streaming is off. The red LED and periodic JEDEC/SFDP reports are preserved.
SPI pins, speed, and the flash erase/program/verification state machine have
not changed. No erase or write runs automatically.

Build passed without reported compiler/linker errors or warnings: RAM 8,060
bytes (3.4%); flash 62,180 bytes (7.7%). All 12 simulation cases passed,
including the prior flash tests plus 10 Hz sampling while muted/disconnected,
printing toggles, the menu, continued LED toggling, and no writes from IMU commands.

## Controlled flash write/read test

**Built only; not uploaded or run on hardware.** The existing IMU readings,
LED timer and read-only JEDEC/SFDP diagnostics remain. Flash SPI stays at 1 MHz.
The new test is disarmed on every boot and every completion; it never starts
automatically. The final 4 KB sector is reserved as the temporary test area.

After upload is separately approved, open a serial monitor at 115200 baud.
The firmware prints a warning that **ALL data at 0xFFF000-0xFFFFFF will be
permanently erased**, and that test data will remain afterward. Only after
reading that warning, send the exact line below followed by Enter (CR, LF or
CRLF; PlatformIO's `send_on_enter` filter can be used):

```text
ERASE FFF000
```

Each accepted line authorises exactly one test. Input received while a test is
active is discarded. Partial input is cleared on disconnect; overlong lines
and other text are rejected. There is no saved confirmation across a reboot.

Test sequence:

1. Recheck JEDEC five times; require `0B 40 18` on every read.
2. Poll status register `0x05` until ready (1 second timeout).
3. Issue `0x06` and verify WEL=1/WIP=0; erase only `0xFFF000` with `0x20`.
4. Poll WIP every 2 ms with a 5 second erase timeout. Verify all 4096 bytes
   read as `0xFF` using standard read `0x03` before programming.
5. Re-enable writes and program one aligned 256-byte page with `0x02`, using
   the pattern `byte_offset XOR 0xA5` at `0xFFF000-0xFFF0FF`.
6. Poll WIP with a 1 second program timeout, read all 256 bytes using `0x03`,
   compare every byte, and report PASS/FAIL plus every mismatched absolute
   address and expected/actual byte.

The implementation has no full-chip erase or protection/status-register-write
command. It does not unlock protected areas or restore erased data. On timeout
or an uncertain status/WEL response, all further flash commands are blocked
until reboot, while IMU and LED service continue. Wait for flash to settle
before resetting. Other flash diagnostics are paused while the test runs.
The state machine avoids blocking delays during erase/program polling.

Build: RAM 8,028 bytes (3.4%); flash 61,444 bytes (7.6%); no reported
compiler/linker errors or warnings. Eleven simulated tests exercised the actual
production state machine with mocked SPI/USB serial (no hardware access):
confirmation gating, successful readback and explicit repetition, wrong ID,
denied WEL, mismatch reporting, erase verification failure, busy/erase/program
timeout lockout, disconnected partial input, and overlong input.

Run the simulator tests with:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" tests/flash_write/run_tests.py
```

It uses the project's ARM compiler and project-local `unicorn` and `pyelftools`
packages in `.pio-core/test-python` to execute ARM code against a mock flash.
The historical read-only diagnostic details below refer to the previous build.

## External flash JEDEC / SFDP diagnostic (2026-10-27)

The current build preserves the IMU output and LED timer and adds an external
SPI flash JEDEC ID and SFDP probe. Connect SCK=D8, MISO=D9, MOSI=D10, CS=D2,
VCC=3V3, and GND=GND. It uses SPI mode 0, MSB first, at 1 MHz.
Only read commands `0x9F` (JEDEC ID) and `0x5A` (SFDP) are issued.
SFDP uses a three-byte address and eight dummy clocks. No write-enable,
program, erase, status-register write, reset or mode-change commands are issued.

At USB serial 115200 baud, on connection and every two seconds, it reads the
JEDEC ID five times, prints each manufacturer/type/capacity tuple, and checks
all five for equality. Consistency alone does not prove a chip identity.
`0B 40 18` is labelled as a possible XTX XT25F128B, not a confirmed model;
uniform all-zero/all-FF IDs report not detected.

The diagnostic prints the eight-byte SFDP header, validates the signature,
revision and access protocol, and scans up to 16 parameter headers. It selects
the newest supported major-1 Basic Flash Parameter Table (BFPT), checks its
length and pointer, then reads its first nine little-endian DWORDs. Density is
decoded using either linear or power-of-two encoding. Output includes exact
bits/bytes and Mibit/MiB capacity, advertised dual/quad fast-read opcodes with
mode/wait clocks, and erase-type opcodes with block sizes. All advertised
commands are **reported only, never executed**. This is a BFPT read/erase
summary, not an exhaustive decoder of optional SFDP tables or program/status
commands. Invalid/unsupported data produces a clear message rather than an
inferred capacity. IMU readings and the independent LED timer are retained.

Field encodings were checked against the
[Linux SPI NOR SFDP implementation](https://github.com/torvalds/linux/blob/master/drivers/mtd/spi-nor/sfdp.c).

Build passed without compiler/linker errors or warnings: RAM 7,700 bytes
(3.2%); flash 57,268 bytes (7.1%). This JEDEC/SFDP build has **not been
uploaded** or verified against hardware. The preceding JEDEC-only build was
uploaded and returned `0B 40 18`. The upload/verification below refers to the
earlier IMU-only build.

## Board and library

Uses Seeed's official PlatformIO platform, board ID
`seeed-xiao-afruitnrf52-nrf52840`, and its shared XIAO Arduino variant.
See https://wiki.seeedstudio.com/xiao_nrf52840_with_platform_io/.

Seeed Arduino LSM6DS3 v2.0.7 is included unmodified in
`lib/Seeed_Arduino_LSM6DS3` (commit
`e063c3a544b8bb89238accddf24f0028bcd1ca20`). The
`TARGET_SEEED_XIAO_NRF52840_SENSE` build flag routes the library to the
internal Wire1 bus. The library enables IMU power on Arduino pin 15 (P1.08)
and addresses the LSM6DS3TR-C at 0x6A.
See https://wiki.seeedstudio.com/XIAO-BLE-Sense-IMU-Usage/.

## Behavior

- Enables accelerometer and gyroscope at 104 Hz sensor sample rate.
- Reports X/Y/Z acceleration in g and angular velocity in degrees/second
  every 100 ms over USB CDC serial at 115200 baud.
- Configures +/-8 g acceleration and +/-1000 dps gyroscope range.
- Toggles the red LED every second, retaining the original test's two-second
  full on/off cycle. Green and blue stay off.
- Repeats a clear error with the initialisation status if IMU startup fails;
  LED blinking continues. Does not print fake readings on startup failure.
- Operates without waiting for a serial monitor connection.

The RGB LED is active LOW. Board constants map red to Arduino pin 11/P0.26,
blue to 12/P0.06, and green to 13/P0.30.

## Build

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run
```

Packages are isolated in `.pio-core/`; artifacts are in
`.pio/build/seeed_xiao_nrf52840_sense/`.

## Upload and verification

Uploaded on 2026-10-26 using the bundled Adafruit nrfutil serial DFU tool
with `firmware.zip`, 115200 baud, and single-bank mode. The XIAO entered
bootloader COM7 after a 1200-baud USB reset, then returned as application
COM8. PlatformIO's direct upload previously passed a HEX file to the ZIP
package uploader; use the compiled ZIP with nrfutil instead.

Build passed: RAM 7,692 bytes (3.2%); flash 52,636 bytes (6.5%).
USB serial verification collected 41 readings with timestamp intervals of
exactly 100 ms and no initialisation errors. Example:

```text
t_ms=8700 | accel_g X=-0.1427 Y=-0.1682 Z=1.0098 | gyro_dps X=1.601 Y=-3.500 Z=0.884
```

Values are raw sensor conversions without bias calibration. Physical LED
flashing was not visually verified.
