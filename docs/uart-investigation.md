# UART investigation and temporary loopback test

**Current baud update:** normal GPS and loopback now run at **38400 baud**,
matching the user's valid-NMEA scan. The inspection below records the earlier
9600-baud setup; pin mapping and core behavior are unchanged. The fixed payload
still contains the legacy text `TACTICPOD_UART_9600`; it does not select baud.

Inspected on 2026-10-29 before adding the loopback command. This build has
**not been uploaded**; no physical loopback result has been obtained.

## Findings from the installed project and BSP

- `platformio.ini` selects Seeed's `seeed-xiao-afruitnrf52-nrf52840` board,
  Arduino, and `Seeed_XIAO_nRF52840` variant. The Sense build flag selects
  Wire1 in the IMU library; it does not remap UART pins.
- The installed framework package is `framework-arduinoadafruitnrf52-seeed`,
  version `1.10101.0`. Its variant header defines RX=7, TX=6; the pin map
  maps D7 to P1.12 and D6 to P1.11.
- `cores/nRF5/Uart.cpp` constructs `Serial1` on `NRF_UARTE0` from those
  Arduino pin indices, translates them through `g_ADigitalPinMap`, and writes
  the result to `PSEL.RXD`/`PSEL.TXD` at begin. `begin(9600)` selects hardware
  9600 baud, 8N1 and disabled hardware flow control.
- GPS reception reads `Serial1.available()/read()`. USB `Serial` is used
  for monitor commands and output. There is exactly one GPS UART reader;
  RAW sees bytes before the normal parser, including binary/NUL bytes.
- `Serial1.begin(9600)` is called once from GPS setup. There were no UART
  `end()`, `setPins()`, baud changes, UART writes or `flush()` calls in the
  application before this explicit loopback test was added.
- No application or variant initialisation reconfigures D6/D7. External SPI
  is D8/D9/D10 with CS D2; IMU I2C is on the internal Wire1 pins. The IMU
  power pin is P1.08, distinct from UART P1.11/P1.12.
- The command parser discards some **USB Serial** input before its warning
  and after consuming a destructive confirmation. It does not discard
  `Serial1` data. Normal GPS parsing continues with printing OFF and during
  flash operations. RAW display-buffer overflow is separately reported.
- The BSP's hardware RX buffer is 64 bytes. RX interrupt handling transfers
  bytes into this ring buffer. Excess traffic or excessive foreground delay
  can lose buffered data, but the audit does not establish that as the cause
  of a zero-byte result.

No incorrect UART mapping, wrong serial object, conflicting pin use or UART
reinitialisation was found. A zero-byte capture cannot identify the cause by
itself: the physical loopback tests the XIAO TX/RX path independently of the
GPS breakout, power supply and cabling.

## Physical procedure, after upload approval

1. Disconnect **both GPS TX and GPS RX wires** from XIAO D7/D6. Do not bridge
   D6 to D7 while GPS TX is still attached: that connects two outputs.
2. Connect XIAO **D6 TX directly to D7 RX** with a short jumper.
3. Open USB Serial Monitor at 115200 baud. Send exactly:

   ```text
   UART LOOPBACK TEST
   ```

4. Wait approximately 1.05 seconds for PASS/FAIL, then remove the bridge and
   restore GPS TX->D7 and GPS RX->D6 before using GPS commands again.

Expected successful output:

```text
UART LOOPBACK TEST PASS | expected=32 | tx_bytes=32 | rx_bytes=32 | mismatches=0 | pretest_discarded=0 | pattern matched
```

The 32-byte pattern is `TACTICPOD_UART_9600` followed by bytes
`00 55 AA FF 01 7F 0D 0A 33 CC 5A A5 21`. This includes binary bytes, is not
a UBX packet or NMEA configuration command, and is transmitted only on the
explicit command. Nothing runs automatically at boot.

The test has a 50 ms quiet-input phase. Bytes found then are counted in
`pretest_discarded`; the test refuses to transmit if RX was not quiet. After
transmission, it compares every received byte, detects wrong/extra/missing
bytes, and reports after a one-second receive window. No jumper normally
produces `tx_bytes=32`, `rx_bytes=0`, FAIL. A new explicit command can retry
after the report. Corrupt/extra data also fails. Test bytes are routed away
from GPS parsers so echoes do not create false GPS fixes or message counts.
Normal GPS reception resumes afterward at the unchanged baud and mapping.

## Avoiding blocking writes in this installed core

The BSP `Uart::write()` uses a semaphore with `portMAX_DELAY` before each
DMA batch. The test fits in one batch (32 <= `SERIAL_BUFFER_SIZE`, 64), and
never calls `flush()` or performs a sequence of blocking per-byte writes.
The first explicit test uses the TX semaphore initialised by `begin()`;
there are no other UART writers. Subsequent tests require hardware
`TXD.AMOUNT == 32` and `EVENTS_ENDTX == 0`, matching the installed IRQ handler
which clears ENDTX then returns the TX semaphore. A stuck transmission is
refused before another write instead of waiting indefinitely. This readiness
check is specific to the audited nRF52 BSP and must be reviewed if it changes.

All waiting is through elapsed-millisecond state transitions, including across
timer wrap. USB output waits for sufficient available TX space without a
blocking loop. IMU sampling, LED service and existing recording continue.
The test does not start recording or issue any flash operation. UART RAW and
loopback tests are mutually exclusive; CSV export refuses loopback to protect
its output. Results are retained if USB is unavailable. Existing flash safety
interlocks and the binary recording layout are unchanged.

## Validation

Build succeeded with no reported compiler/linker errors or warnings:
RAM 27,248 bytes (11.5%); flash 90,580 bytes (11.2%). All 70 simulations pass:
13 flash regressions, 20 recording cases and 37 GPS/UART cases. New cases cover
PASS/FAIL comparisons, retry, RX noise, short/stuck TX, duplicates, RAW/CSV
interlocks, USB backpressure, timer wrap, reception resumption and 100 Hz
recording alongside loopback. The previous GPS/RAW cases also assert that they
never transmit UART data. Simulation does not establish a physical UART result.
