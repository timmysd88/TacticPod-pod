# Read-only GPS CONFIG diagnostic

This build has **not been uploaded**. No receiver configuration values have
been measured using the new diagnostic yet. Its decoded test output is simulated,
not a report of this particular receiver's settings.

After an approved upload, connect USB serial at 115200 baud, wait for
`RECORD scan complete` and send `GPS CONFIG` followed by Enter. Keep the normal
GPS wiring: TX to XIAO D7 RX, RX to D6 TX. Serial1 stays at 38400 baud, 8N1.
Wait for `GPS CONFIG RESULT` through `GPS CONFIG END`. Collection usually takes
about ten seconds; entirely unanswered requests take about 46 seconds. A
55-second overall collection deadline bounds abnormal scheduling delays.

The command is refused during recording, stop flushing, startup scan, erase,
CSV export, recorder faults, flash diagnostic tests, or another GPS diagnostic.
Recording and flash commands remain interlocked until the result is delivered.
GPS ON/OFF still changes the normal printing preference; automatic GPS reports
pause during CONFIG and resume afterward if enabled. Reception, NMEA parsing,
IMU acquisition and LED service continue. A USB disconnect or full USB buffer
does not delay UART collection; the bounded report waits for reconnection/space.
No command runs automatically at boot.

## Values read

The fixed whitelist reads these current RAM settings using UBX-CFG-VALGET,
version 0, layer 0, position 0:

- Measurement interval (`CFG-RATE-MEAS`, milliseconds), measurements per
  navigation solution (`CFG-RATE-NAV`) and decoded navigation time reference.
- GPS, Galileo, BeiDou, GLONASS, QZSS and SBAS enable flags; GPS L1 C/A,
  Galileo E1, BeiDou B1I/B1C and GLONASS L1 signal enables.
- UART1 baud rate, UART1 enable, UBX input enable and UBX/NMEA output enables.
- UART1 output divisors for NMEA GGA, RMC, GSA, GSV, GLL, VTG, ZDA, GST, GBS,
  GRS, DTM, RLM and VLW; selected UBX NAV-PVT, NAV-POSLLH, NAV-STATUS and NAV-SAT.
  Zero disables a message. A nonzero divisor is relative to navigation epochs;
  output also requires its protocol gate. Message families can produce several
  sentences per epoch, so these values alone do not predict total UART traffic.
- Power operating mode, peak-current limiting, dynamic platform model, fix
  mode and maximum navigation satellites. Newer firmware-only fields may be
  rejected on this MAX-M10S firmware; rejected fields do not abort other queries.

An empty UBX-MON-VER poll also reads software/hardware identity and up to fifteen
30-byte extensions, including firmware/protocol versions when advertised.
Terminal control bytes in returned strings are replaced with `?`.

Four additional **read-only** VALGET polls use the MAX-M10S integration manual's
documented OTP verification layer 4 and keys `0x40A40001`, `0x40A40003`,
`0x40A40005`, `0x40A4000A`. Their returned unsigned values are displayed in Hz.
The documented high-clock OTP profile is 192000000, 192000000, 192000000,
96000000. Matching it verifies the stored startup profile, **not the currently
running CPU clock** or whether a reset has applied a recent change. Unsupported
OTP queries are reported unavailable/rejected. The manual's OTP programming
sequence is never included or transmitted.

## Protocol and safety

Only MON-VER (`0A 04`, empty poll) and CFG-VALGET (`06 8B`) are transmitted.
There are no VALSET, VALDEL, reset, OTP-programming, save or flash commands.
Reading the GPS RAM/OTP layers does not write them. TacticPod SPI flash is not
accessed by the diagnostic, and its recorder format/layout and safety logic
remain unchanged.

There is one UART reader. A bounded UBX state machine consumes UBX headers,
payload and checksum before TinyGPSPlus sees NMEA bytes. A `$` inside a UBX
payload cannot start a normal NMEA sentence. RAW still captures/counts every
byte, including complete UBX frames. No line reads, blocking delays, UART
flushes, baud changes or receiver resets are used.

Each poll requests one distinct key. To avoid a stale DMA byte count being
mistaken for TX completion, requests alternate 16 and 20 bytes by requesting
the same key twice in every second request. Official VALGET semantics return
both key/value pairs; both must agree. This stays within one BSP UART DMA batch.
The shared TX guard also preserves the existing explicit loopback test and baud
scan preflight. CONFIG stops polling if TX completion is uncertain.

UBX checksums cover class through payload. Replies must match the outstanding
key, layer, response version 1, position 0 and exact expected typed length.
Logical values must be 0 or 1. Buffers are fixed: 512-byte UBX payload,
490-byte MON-VER identity, 20-byte outgoing packet and 256-byte USB report line.
The existing single reader handles at most 64 UART bytes per main-loop pass.
An incomplete frame expires after a 100 ms gap or 250 ms total; oversized or
corrupt frames are never decoded as configuration values. Corrupt framing can
therefore discard up to 250 ms of incoming data before resynchronisation.

There is one outstanding poll, an 800 ms response deadline, a 200 ms separation
between polls and a 1-second TX-ready deadline. Late/wrong-key responses cannot
satisfy a subsequent query. ACK-NAK only identifies message class/ID, not the
requested key: a severely delayed NAK can remain ambiguous despite spacing.
This is why the result says `UNSUPPORTED/REJECTED`, not proven hardware absence.
A timeout means `UNAVAILABLE`, not unsupported: UBX input/output may be disabled,
the GPS RX wire may be missing, or a reply may have been lost. ACK-ACK alone
does not count as a returned configuration value. No automatic unlock or
configuration fallback is attempted.

## Interpreting 10 Hz capability

The report derives the **requested** navigation rate as
`1000 / (MEAS_ms * NAV)`. This is not a measured update/fix rate. Run the existing
`GPS RATE` separately to measure checksum-valid GGA/RMC cadence without a fix.
Power mode FULL means power saving is disabled; it does not prove the high CPU
clock profile is active. Actual limits depend on firmware, enabled signals,
constellation combinations and applied clock configuration.

The MAX-M10S R08 data sheet lists these example maximum navigation rates:

| Constellations | Default clock | High performance |
| --- | --- | --- |
| GPS + Galileo | 10 Hz | 20 Hz |
| GPS + Galileo + GLONASS | 6 Hz | 16 Hz |
| GPS + Galileo + BeiDou B1I | 3 Hz | 12 Hz |
| GPS + Galileo + BeiDou B1C | 8 Hz | 16 Hz |
| GPS + Galileo + BeiDou B1C + GLONASS | 4 Hz | 10 Hz |

These are data-sheet conditions, not measurements of this breakout. The
diagnostic deliberately reports `10Hz support NOT PROVEN`. It cannot infer
live CPU speed from stored OTP alone or guarantee fixes under indoor conditions.
At 38400 baud, 8N1, the UART budget is 3840 bytes/s. Use measured sentence
lengths/rates from GPS RATE to budget output, including multi-sentence GSV/GSA
and any other traffic. No 10 Hz setting is applied.

## Official references

- [M10 SPG 5.00 interface description](https://content.u-blox.com/sites/default/files/u-blox%20M10-SPG-5.00_InterfaceDescription_UBX-20053845.pdf): VALGET, MON-VER, original M10 keys including GLONASS.
- [M10 SPG 5.21 interface description](https://content.u-blox.com/sites/default/files/documents/u-blox-M10-SPG-5.21_InterfaceDescription_UBXDOC-304424225-20430.pdf): current key definitions and duplicate-key VALGET response semantics.
- [MAX-M10S integration manual, R04](https://content.u-blox.com/sites/default/files/MAX-M10S_IntegrationManual_UBX-20053088.pdf), section 2.1.7: high-performance clock and read-only OTP verification.
- [MAX-M10S data sheet, R08](https://content.u-blox.com/sites/default/files/MAX-M10S_DataSheet_UBX-20035208.pdf), table 2: constellation-dependent navigation limits.

## Simulation

Run from the project directory using the installed PlatformIO Python:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" tests/flash_write/run_tests.py
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" tests/recording/run_tests.py
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" tests/gps/run_tests.py
```

The production ARM code is exercised with mock UART/NOR/USB devices. New cases
cover outbound poll checksums/allowlist, RAM and OTP decoding, rejection versus
timeout, checksum/type/version/layer/key/length validation, duplicate agreement,
simultaneous NMEA, fragmented/oversized/stalled frames, deadline/wrap behavior,
USB disconnect/backpressure, recorder/diagnostic interlocks, stale/stuck TX,
post-CONFIG loopback/SCAN/RAW, and 100 Hz recording with unsolicited UBX traffic.
Simulation cannot confirm actual receiver key support, electrical TX wiring,
real UART scheduling or the live CPU clock. Hardware verification awaits upload
approval.
