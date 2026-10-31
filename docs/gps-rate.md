# Non-destructive GPS RATE diagnostic

This build is **not uploaded**. Normal UART stays at 38400 baud, D7 RX / D6 TX.
No GPS configuration, UART transmission, flash write or recording-format change
is added. The command can run while 100 Hz IMU recording is already active;
it does not start or stop recording itself.

## Usage after approved upload

At USB serial 115200 baud, send:

```text
GPS RATE
```

Wait 15 seconds for `GPS RATE RESULT` through `GPS RATE END`. The diagnostic
observes the existing single UART reader before normal TinyGPSPlus parsing,
without taking bytes away from that parser. It counts complete, CRLF-terminated,
checksum-valid GGA/RMC sentences even with empty coordinates and no satellite
fix. All two-letter talker IDs are accepted, including GP and GN. Other valid
NMEA messages contribute to total traffic but not the position-message counts.
Frames exceeding 191 characters, partial pre-window frames, messages ending
after the window, malformed checksums and corrupt sentences are not counted as
position messages. This diagnostic never waits for a fix or a whole sentence.

The result includes separate GGA and RMC rows for:

- Sentence count and average sentence rate over the fixed 15000 ms window.
- Valid-fix, no-fix and unknown-status message counts. GGA quality 1-5 indicates
  a fix; quality 0 indicates no fix. RMC A indicates valid and V indicates void.
- Forward UTC epochs, epoch rate, duplicate times, missing times and backwards
  times. UTC is parsed to milliseconds; midnight wrap is handled. UTC with
  seconds 60 (leap-second notation) is treated as unavailable in this version.
- Mean wire bytes per sentence, including `$`, checksum and CRLF.

GGA and RMC are **never summed to estimate GPS epochs**. If both report the
same 150 epochs, that is approximately 10 Hz, not 20 Hz. Repeated GP/GN GGA
messages with the same UTC timestamp are also counted as duplicate epochs.
The primary family is RMC whenever checksum-valid RMC sentences are available,
matching the configured 10Hz RMC / 1Hz GGA stream. GGA is reported independently
with `role=SUPPORTING`; it supplies the fallback cadence only when RMC is absent.
Missing or frozen RMC UTC produces UNKNOWN even if GGA supplies advancing UTC.

`output_cadence_10hz=YES (approximately)` means 143-157 advancing UTC epochs in
15 seconds (within roughly 5% of 10 Hz). Thus 149 advancing RMC epochs
(9.933Hz), with 15 GGA epochs (1Hz), reports primary=RMC and YES. This tolerance
allows complete sentences that straddle the measurement boundaries to be lost.
The displayed counts and rates remain exact. It is a window average, not a jitter
or latency guarantee. Frozen/missing UTC cannot prove epoch rate and produces
UNKNOWN, while sentence rates still work indoors. No-fix messages can establish
output cadence but **cannot prove 10 Hz valid navigation solutions**.

## Nonblocking behavior

The rate observer processes each received byte in bounded RAM with a fixed
192-byte sentence buffer. No new UART reads or peripheral register writes are
used. The 15-second timer works across `millis()` wrap. Results use bounded
complete USB lines, written only when TX space is available. Collection and
normal parsing continue with USB disconnected; reporting waits for reconnection
or the end of CSV export.

Regular GPS printing pauses during the diagnostic and resumes afterward if
enabled. `GPS OFF` does not cancel measurement. Duplicate RATE, RAW, SCAN,
loopback and GPS STATUS requests are refused until the report is complete.
Rate measurement is refused during an existing UART diagnostic or CSV export.
An export started after measurement begins defers RATE output, preserving CSV.
Recording commands, IMU sampling, flash protections and LED service remain
unchanged. GPS values are not added to the existing binary IMU format.

## Observed current output, without uploading

During this task, the already installed firmware's receive-only `GPS RAW`
command was used for a ten-second sample. No new firmware was uploaded, no GPS
configuration was sent, and COM8 was closed afterward. The local aggregate
artifact is `.pio/gps-rate-baseline.json` (no coordinates are stored in it).

| Measurement | Result |
| --- | --- |
| Raw bytes | 5700, with no omitted bytes |
| Checksum-valid NMEA sentences, all types | 140 |
| GGA sentences / distinct UTC epochs | 10 / 10 |
| RMC sentences / distinct UTC epochs | 10 / 10 |
| GGA and RMC output rate | 1.0 Hz each |
| Valid-fix GGA/RMC messages | 0 / 0 |

Current **position-message output is 1 Hz, not 10 Hz**, in this sample. Internal
receiver navigation rate was not queried: output-message divisors could differ
from the internal rate. The new command will provide a separate 15-second
measurement after approved upload.

## UART bandwidth

38400-baud 8N1 uses ten serial bits per byte, so maximum payload is **3840
bytes/second**. Observed current traffic is **570 bytes/second** (~14.8%).

- The no-fix sample's GGA and RMC were 42 bytes each: sending both at 10 Hz
  would use 840 bytes/second at those lengths. Fix-bearing sentences can be
  longer. Assuming about 80 bytes each gives about **1600 bytes/second**,
  ~42% of link capacity, leaving room for a restrained auxiliary message set.
- Scaling **every current message** by ten would require about **5700
  bytes/second**, beyond 3840. This is not a safe blanket change. Keep optional
  messages at lower rates, limit the set, or later consider a higher UART rate.
- RATE reports current_Bps, UART_capacity_Bps, GGA_RMC_only_at10Hz_Bps using
  measured mean sentence lengths, and all_stream_scaled_to10Hz_Bps assuming all
  traffic scales with the primary family's epoch rate. Missing families or
  insufficient advancing UTC produce N/A. These are estimates: fix acquisition,
  satellite count, GSV volume and other output settings can change traffic.
- The BSP's 64-byte RX ring has about 16.7 ms capacity at maximum line rate.
  Physical timing/interrupt losses still need real-device validation; the rate
  reports only bytes delivered by the driver and checksum-valid messages.

MAX-M10S navigation limits also depend on constellation and performance mode;
the latest [u-blox MAX-M10S data sheet](https://content.u-blox.com/sites/default/files/MAX-M10S_DataSheet_UBX-20035208.pdf)
lists different maxima for different configurations. Achieving 10 Hz valid
navigation may require a later explicit configuration step. Nothing here changes
navigation rate, message settings, performance mode or constellations.

## Validation

All 106 simulations pass: 13 flash, 20 recording, 73 GPS/UART. New cases cover
1/10 Hz data, indoor no-fix frames, valid-fix counts, CRC rejection, duplicate
talkers, RMC-only output, missing/frozen/backwards UTC, no data, the exact
15-second boundary, USB backpressure/disconnect, CSV protection, interlocks,
timer/midnight wrap, overlong input, and concurrent 10 Hz NMEA with 100 Hz
recording (1500 committed samples with no simulated loss or missed deadlines).
No simulated test accesses hardware. The separate baseline capture above used
only the pre-existing receive-only RAW command.

Build passed with no reported compiler/linker errors or warnings: RAM 65,728
bytes (27.7%); flash 101,984 bytes (12.6%).
