# Receive-only GPS baud scan

This build is **not uploaded**. No physical scan has been run. The command
temporarily changes the XIAO receiver, never the GPS module's settings, and
sends no UART payload or flash commands.

## Procedure after an approved upload

Keep normal GPS wiring: GPS TX to D7 RX and GPS RX to D6 TX.
**Remove any D6-D7 loopback jumper.** Open USB serial at 115200 baud, wait for
startup recovery, check `RECORD STATUS` reports STOPPED, then send:

```text
GPS SCAN
```

It receives for three seconds each at 4800, 9600, 19200, 38400, 57600 and
115200 baud. Allow about 18 seconds plus receiver transitions. Results are
retained in RAM and printed after restoring 38400. USB backpressure/disconnection
does not postpone restoration. Normal GPS reception resumes before the report
is fully printed. Background GPS output pauses during the scan.

A scan is refused while recording, flushing, exporting, erasing, startup
recovery, flash fault, RAW/loopback, or incomplete UART TX owns resources.
During scanning, recording starts, CSV export, flash diagnostics and erases are
refused; `RECORD STATUS` and `RECORD STOP` remain available. No scan runs at boot.

## Results and hardware checks

Each rate reports elapsed measurement milliseconds, all bytes delivered by
the driver (including binary/NULs), checksum-valid NMEA sentences, and observed
framing/parity/overrun/break flags. Before, per-rate and after snapshots show
actual nRF52840 registers: PSEL RXD/TXD, ENABLE, BAUDRATE, RXSTARTED/RXTO,
RX DMA pointer/MAXCNT, ENDRX interrupt enable and CONFIG.

Expected values are RXD `0x0000002C` (44 = P1.12 = D7), TXD `0x0000002B`
(43 = P1.11 = D6), ENABLE `8`, MAXCNT `1`, RX interrupt enabled,
RXSTARTED set, RXTO clear and a nonzero RAM DMA pointer. The 38400-baud register
is `0x009D0000`. Events are evidence of start/stop; no separate readable live
"RX active" status bit is claimed. Unexpected initial configuration is
reported without silently remapping pins.

The final `GPS SCAN END` reports `restored_38400=YES` only after restart and
matching hardware configuration are observed. Stop/start timeouts abort the
scan and attempt recovery to 38400. If recovery cannot be confirmed, the report
says `restored_38400=NO; reset required`. Further UART diagnostics/parsing are
blocked until reboot; the independent recorder remains usable after the report.

### Error-count limitations

The core has no public error counters. The scan polls `ERRORSRC` and clears
only observed bits with write-one-to-clear semantics. Reported counts are
**observed sticky flags/lower bounds**, since repeated errors between polls
can coalesce. Normal 8N1 disables parity checking: a zero parity count labelled
`disabled/8N1` cannot establish correct parity. If the existing CONFIG enables
parity, its observed flags are labelled enabled. The scan preserves CONFIG.

Nordic documents that a framing-error byte is not placed in RAM, while a
parity-error byte can be. Thus bytes and error counts describe different
observations. Software RX-ring overflow is not separately exposed by this BSP.
Transition-discarded bytes are counted separately instead of attributed to
the next rate. See the [Nordic nRF52840 specification, UARTE section](https://docs-be.nordicsemi.com/bundle/nRF52840_PS_v1.8/raw/resource/enus/nRF52840_PS_v1.8.pdf).

3.26 V on GPS TX can be consistent with UART idle HIGH; a DC reading does not
demonstrate transmission of bytes. Scan results alone cannot prove breakout
power circuitry or GPS hardware is working.

## Implementation and safety

The installed core's `begin(newBaud)` is a no-op after UART has begun, while
`end()` has unbounded hardware-stop waits. The scan instead uses a timed,
cooperative state machine: mask ENDRX (the core handler otherwise restarts RX),
STOPRX, await RXTO for up to 100 ms, drain/count old buffered bytes, disable
the stopped peripheral, update BAUDRATE, re-enable the RX interrupt, STARTRX,
then await RXSTARTED for up to 100 ms. The three-second window starts after
receiver configuration is verified. The same sequence restores 38400.

Pin selections, CONFIG, core DMA storage, TX semaphore and other interrupt
enables are preserved. The module never calls UART `write`, `flush`, `end` or
`begin`, and does not issue GPS configuration. It depends on the installed
Seeed BSP's single-byte RX DMA implementation and must be reviewed after a
core change. IMU/LED service continues, and flash layout/recording format are
unchanged. Scanned bytes use a separate parser and do not corrupt cached GPS
positions with wrong-baud data.

## Validation

All 88 simulated cases pass: 13 flash, 20 recording, 55 GPS/UART. Cases
cover exact windows, binary bytes, error flag clearing, parity availability,
NMEA, actual-pin/disabled-RX detection, stop/start timeout recovery and failure,
timer wrap, USB disconnect, concurrent-command interlocks, boundary discards,
continued idle IMU/LED service, and loopback after scanning. No tests access
the device. Build: RAM 65,148 bytes (27.4%), flash 96,904 bytes (11.9%), with
no reported compiler/linker errors or warnings.
