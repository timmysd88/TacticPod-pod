# GPS backup RX-event diagnostic candidate

Latest attempt (2026-11-03): approved diagnostic candidate preflight STOPPED on GPS NMEA=NONE/current profile UNVERIFIED. No upload or cycle; see the preflight-stop record at the end.

Date: 2026-11-03. Local implementation and software validation only. No device connection, upload, reset, hardware test, recording, erase, automatic standby enablement, push, merge, tag or release was performed.

The preceding physical test of f6dcdf4c... failed entry confirmation on one 0x32 byte immediately after UART resume. This candidate adds evidence collection; it does not clear a suspected stale event or claim to repair physical standby/wake.

## Exact implementation changes

- `src/gps_backup_uart_diag.h` and `.cpp` (new): five fixed snapshot slots tagged BeforeStop, AfterStop, BeforeRestart, AfterRestart and FirstRxIrq. The first RX interrupt means the first IRQ with ENDRX set after resume is armed, before the BSP handler clears it. TX-only interrupts do not consume that slot. Snapshots include ENDRX, RXDRDY, RXSTARTED, RXTO, ERROR, RXD.AMOUNT, ERRORSRC, INTENSET, ENABLE, NVIC enable/pending/active state, PRIMASK, TXD.AMOUNT and ENDTX. Timestamps are 32-bit microseconds relative to diagnostic start, using the existing TIMER4 MonotonicClock. Slots are published with memory barriers, cannot overwrite the first observation, and reset between cycles.
- `platformio.ini`: only the two GPS backup test environments gain linker defsym/undefined flags selecting `GpsBackupUartDiag_IRQHandler` as the UARTE0 IRQ vector target. It always delegates to `Serial1.IrqHandler()`. Ordinary linker wrapping did not bind this BSP's startup weak reference and was rejected during linked-image verification; the final alias is checked in the actual vector table. No framework/vendor UART driver file was edited. Stock, TCB and normal reusable environments retain the original vector.
- `src/gps_power.h`: `unexpectedRx()` immediately latches the existing EntryNotConfirmed result. The existing Listen phase remains running for a bounded 1500 ms from that first byte; further traffic cannot extend its deadline or convert failure to PASS. A safety abort may end observation early while preserving the latched failure. All successful-path timings, PMREQ bytes, wake mask, retry limits and PASS/recovery criteria remain unchanged.
- `src/gps_receiver.cpp`: snapshot calls surround UART shutdown/restart; the single RX reader latches failure at the first unexpected non-UBX byte. During Drain/Listen, including failure observation, update returns after receiving and sampling errors, before automatic wake, GPS configuration or profile services. Recording remains blocked by the existing running-test guards throughout. Reports print only after the observation/cycle completes. IRQ snapshots are taken before the original handler; they do not clear or modify UARTE events, DMA amounts, errors, masks, pins or buffers. Normal ISR behavior still runs unchanged.
- `src/gps_backup_trace.h`: bounded per-phase NMEA candidate-start, incomplete-ended and open-fragment counters supplement valid sentence, checksum-failure, raw-byte and timing counters. Reports separate isolated bytes, valid complete sentences, incomplete candidates and unclassified traffic. Raw prefixes remain capped at 96 bytes separately for Drain/Listen/Wake while counters keep counting.

The trigger retains the existing non-UBX byte-counter criterion, and the diagnostic trace still sees all raw UART bytes. This patch does not silently discard the first byte or introduce a grace period that can permit PASS. It does not change shared receiver configuration, add baud scanning, send resets or enable automatic backup.

## Focused tests and isolation

Changed test files: `tests/reusable/gps_backup_test.cpp`, `gps_backup_integration.cpp`, `gps_backup_tests.py` and `tests/flash_write/Arduino.h`. The shared host mock gains read-only register/IRQ/barrier helpers and a minimal ISR model; no production UART driver was changed.

Coverage includes the unchanged independent 24-byte PMREQ oracle; failure immediately visible at the RX reader; a fixed failure-observation deadline; no wake/config/profile TX during observation, even after valid NMEA returns; both recording admission paths blocked; no interim diagnostic USB output; continuous complete valid sentences; invalid-checksum sentences; incomplete fragments; an isolated byte; first-IRQ snapshots captured before delegation; TX-only IRQ filtering; preservation of sticky errors; bounded snapshot slots and per-cycle reset. Linked vector/isolation checks cover all seven firmware environments.

Negative controls reject the original circular recovery gate (failure code 11), missing force flag (full-frame oracle failure 161), and removal of the receive-only service guard (unwanted TX failure 55). These are synthetic/ARM-emulated software checks, not physical validation of EasyDMA or pin waveforms.

## Candidate and validation

Selected environment: `tacticpod_reusable_usb_gpsbackup`; automatic GPS standby OFF.

Application BIN size: **211648 bytes**.

SHA-256: **`56d9fb3539fd2b164f432eb5c7fa557c94489b77405494fd17eb28f426b747ad`**.

All seven builds PASS: stock Sense, TCB first-write, append-30s, movement-120s, normal reusable USB, supervised GPS backup and compile-only automatic GPS backup. Stock and normal reusable application BINs are byte-identical to the previous candidate. The selected build uses 125456/237568 bytes static RAM (52.8%) and 211648/811008 bytes flash (26.1%): +448 RAM and +2112 flash bytes versus the previous supervised candidate. Whole-firmware peak stack/timing on physical hardware is not claimed.

Focused tests PASS: eleven receiver integration scenarios, two snapshot models and linked vector checks for seven builds. Linked startup checks and all three negative controls PASS. Full 32-suite regression run: **PASS, 32/32 suites, exit code 0**. All eleven receiver scenarios, both snapshot models and seven linked-vector checks also pass within this complete run.

Evidence, source snapshots and 28 archived build outputs (ELF/HEX/ZIP/application BIN): `logs/hardware-gps-backup-2026-11-03/rx-event-diagnostic/`. See `manifest.json`, `SHA256SUMS.txt`, `build-all-final.txt`, `focused-final.txt`, `compiled-startup.txt`, the negative-control JSON files, and full `regressions.txt`. The prior approved candidate remains separately archived under `force-backup-fix/`; this new checksum is not upload-approved.

## What this can establish

An ENDRX flag and nonzero RXD.AMOUNT already present after shutdown/before restart establish a pending receive event before listening is rearmed. Comparison with the first IRQ snapshot can strongly support the stale-event explanation. A clear before-restart snapshot followed by a fresh event provides a different sequence.

It cannot conclusively prove that a particular byte is old versus new: register reads are sequential, the before-restart snapshot is outside the driver's internal STARTRX sequence, events can occur between snapshots, and no independent wire-arrival record or per-byte DMA generation identifier is captured. ERRORSRC and events are not cleared for diagnostics, so retained errors can predate the test, and repeating an already-set error bit cannot be counted as a new error from these reads. Zero NMEA checksum failures does not establish absence of hardware UART errors. Microsecond register timestamps and millisecond host-read byte timestamps are distinct observations.

Instrumentation adds bounded execution time around the handovers and can perturb their timing; physical timing impact has not been measured.

For conclusive byte-origin and unintended-wake correlation, a separately approved passive capture of MCU TX/GPS RX (D6/P1.11) and GPS TX/MCU RX (D7/P1.12) is still required. No such experiment was performed or scheduled here.


## Approved RX diagnostic candidate: preflight stop, no upload (2026-11-03)

The owner approved one application upload and one supervised cycle of the new diagnostic firmware: `tacticpod_reusable_usb_gpsbackup`, **211648 bytes**, SHA-256 **`56d9fb3539fd2b164f432eb5c7fa557c94489b77405494fd17eb28f426b747ad`**. The attempt stopped before upload because the existing firmware's GPS readiness check failed.

Passed: exact BIN size/hash, source/test/framework/artifact manifest hashes, matching application bytes extracted from ZIP and ELF, linked diagnostic IRQ vector and 1500 ms observation metadata, automatic backup OFF in the selected candidate. The planned DFU package is application-only with existing S140 requirement 0x0123; it does not intentionally erase external SPI recordings. No bootloader-entry request or DFU process was invoked.

COM8 identity matched the confirmed XIAO nRF52840 Sense: VID:PID 2886:8044, unique serial `0E8A89B7289430BD`. Serial communication worked at 115200. Recording was STOPPED, OBS capture/fast IMU OFF, COMP capture 0 with empty queues, TCB idle state 1, session 0, lock 0, error none and programs 0. No framed export was seen and normal status commands received replies; no capture, export or recording was initiated.

The GPS check returned:

```text
GPS STATUS | UART=Serial1 RX:D7 TX:D6 38400 | NMEA=NONE | bytes=11797 | checksum_ok=161 | checksum_fail=0 | last_valid_message_age_ms=N/A
GPS configuration: UNVERIFIED (no receiver communication after wake/start)
GPS ERROR: UART data stopped; check power and TX wiring.
```

The historical 10 Hz profile line and 161 historical valid checksums do not establish current readiness. Cached position age was 1614757 ms. The cause of current silence was not investigated with further device commands, and no assumption of confirmed backup mode is made.

Serial was closed immediately. Fresh JEDEC/SFDP checks were not reached; no current flash failure is established. **Zero uploads and zero supervised cycles** occurred. Therefore there are no new physical UARTE snapshots, first-RX IRQ measurements or 1500 ms passive-observation results. The new candidate remains untested on hardware. The earlier force-bit candidate's one-byte entry failure remains the last completed physical test result.

Evidence directory: `logs/hardware-gps-backup-2026-11-03/hardware-run-rx-diagnostic-approved-one-20261103/`. Files: `candidate-verification.json`, `verified-elf.bin`, complete `pre-upload-serial.txt`, `preflight-result.json`, `hardware-summary.json` and `evidence-manifest.json`. Host orchestration: `approved_rx_diagnostic_one_cycle.py`, invoked only with `prepare`.

No firmware source changes, uploads, extra cycles, UART recovery commands, resets, recording erases, automatic standby enablement, push, merge, tag or release. A fresh preflight must pass before the approved diagnostic cycle can run. No further action was taken.
