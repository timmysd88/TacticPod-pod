# GPS BACKUP TEST cycle 1: offline investigation

Latest hardware result (2026-11-03): the approved force-bit candidate was uploaded once; its one supervised cycle FAILED entry confirmation on a single 0x32 byte immediately after UART resume. No deliberate wake or further cycle; serial closed. See the resumed attempt at the end.

Latest hardware attempt (2026-11-03): preflight STOPPED before upload because the existing GPS reports NMEA=NONE and UART data stopped. Zero new uploads or test cycles; see the approved force-bit candidate attempt at the end.

Latest status (2026-11-03): the authorized local force-bit correction has passed software validation. The new candidate is not uploaded or hardware-verified. See the authorized local correction follow-up below for its checksum, complete file list and results.

Date: 2026-11-03. **Read-only investigation: no device connection, reset, upload, rerun, firmware/test changes, automatic standby, merge or push.** Only saved evidence and source were read; documentation and offline evidence files were produced.

## Finding

**The leading cause is a documented command-flag mismatch, with insufficient RX evidence to prove the exact physical failure mechanism.** The approved frame sets `backup` only (`flags=0x02`). The module-specific requirement and recommended correction are recorded under Documentation comparison below. The report's assertion that the receiver kept producing NMEA is stronger than the actual test observation: the trigger was a change in a non-UBX byte counter after UART restart. Neither complete NMEA nor its arrival timing was captured.

The test stopped in **Listen / EntryNotConfirmed**, before its explicit Wake, Profile or Fix phases. It did not demonstrate a failed wake. The earlier recovery-gate correction was not the cause of this early branch: that correction concerns later profile verification/readiness.

## Exact approved source and evidence

- Source base: `29f90966d868f68b85d63d64d27bab9584f7628a`, plus the approved uncommitted recovery-gate patch.
- Patch SHA-256: `0a4ffaa21e61a320e9e30ef2f3b79271afc2f757808063fd0c1312c876aa8eaa`.
- Uploaded BIN SHA-256: `aced1874981173ebfbb3904390f6088214aca3419f72f6a5fae0deb4f4c14051`.
- DFU ZIP SHA-256: `91727831db63198d2c007134803526125a096adc47eeecbe1d8c3879e2004e28`.
- Full saved hardware transcript: `logs/hardware-gps-backup-2026-11-03/hardware-run-approved-20261103/post-upload-and-cycles-serial.txt`.
- Cycle response/results: `cycle-1-response.txt`, `cycle-1.json`, `test-result.json`, and the upload provenance files beside that transcript.

The source/test and build artifact hashes were rechecked against the approved manifest and still match. The installed BSP's `Uart.cpp`/`RingBuffer.cpp` were inspected to trace the actual serial implementation used by this build. Analysis and input-file hashes: `logs/hardware-gps-backup-2026-11-03/gps-backup-cycle1-investigation/findings.json`. The analysis script does not import a serial library or open a device.

## Answers to the seven requested checks

### 1. Was the backup request transmitted successfully?

**Confirmed at the MCU write API; full on-wire delivery is not independently proven.**

`backupStep()` calls `GpsUartTx::write(frame, 24)` and records success if the returned count is 24. The core copies the batch into its DMA buffer, starts UARTE TX and returns the scheduled count. The saved `standby request sent: YES` therefore establishes that the batch was accepted by this API.

The test then normally waits for `GpsUartTx::ready()` (TXD.AMOUNT equals the previous size and ENDTX has been cleared by the ISR), with a minimum five milliseconds from `requestSent()`. Its fallback proceeds after 105 ms even if readiness is still false. It does not log which branch occurred, the observed TXD.AMOUNT, ENDTX, completion time or a wire capture. The fallback is not proof of successful complete transmission and could stop an incomplete transfer. Normal startup UBX transactions worked, so there is no positive evidence of a broken TX link; this individual request's physical delivery remains unmeasured.

The source deterministically builds this well-formed 24-byte UBX packet:

```text
B5 62 02 41 10 00
00 00 00 00 00 00 00 00 02 00 00 00 08 00 00 00
5D 4B
```

This is a reconstruction from the hash-verified source, **not a packet captured at the receiver**. Its checksum is internally consistent. The fields are version 0, duration 0, flags 2 and wake mask 8. At 38400 baud, 8N1, 24 uninterrupted bytes take 6.25 ms on the wire (calculated, not measured).

### 2. Did the receiver acknowledge it; was acknowledgment expected?

**No PMREQ acknowledgment is recorded or checked; an ACK is not expected for this operation.** u-blox's own [`ubxlib` power implementation](https://raw.githubusercontent.com/u-blox/ubxlib/master/gnss/src/u_gnss_pwr.c), in `uGnssPwrOffBackup()`, explicitly treats RXM-PMREQ as unacknowledged and sends it without a response buffer. A missing CFG-style ACK is therefore not the test failure.

The approved firmware has no PMREQ-specific ACK/NAK observer. Its UBX demultiplexer consumes recognized UBX traffic; the profile acknowledgment handler is for its own configuration requests, not class/id `02 41`. The USB transcript is not a raw UBX capture. It cannot establish whether any unsolicited response was physically received, and a successful write must not be described as receiver acknowledgment.

The reference library also uses a generic power-off payload/mask, with optional power-control GPIO handling. That is not a reason to copy its payload indiscriminately into this module-specific software-standby test; the MAX-M10S manual controls the flag choice.

### 3. What were the exact timing and bytes during the silence window?

**Not recoverable from the saved evidence.** No phase timestamps, raw Serial1 samples, listen-byte count, UART error flags or DMA RX events were saved. The host transcript contains USB status/report text without per-byte timestamps. `cycle-1.json` contains the same summary, not a hidden UART trace.

The source-only timing sequence is:

| Event | Configured behavior; not measured timing |
| --- | --- |
| Queue request | One 24-byte DMA batch. |
| Drain | Ready plus at least 5 ms from the write callback; otherwise proceeds at 105 ms. |
| Park | `Serial1.end()`, then D6 configured OUTPUT/HIGH; nominal parked dwell 3000 ms. No reception/observation during this dwell. |
| Resume | Baseline `byteCount` is taken **before** `Serial1.begin(38400)`; software receive state is re-armed. |
| Listen | Nominal silence target 1500 ms. Any subsequent counter change causes immediate failure on the next `backupStep()`. The full 1500 ms need not elapse. |
| Explicit wake | Not reached; no `0xFF` wake burst was sent. |

No exact first-byte offset, hex contents, arrival cadence, actual dwell/listen duration or absolute receiver timeline can honestly be supplied. Filesystem timestamps cannot replace this missing instrumentation.

### 4. Valid complete NMEA, residual bytes, or unrelated data?

**INCONCLUSIVE.** `pollUart()` first passes bytes to `GpsConfig::consume()`. Recognized UBX bytes are filtered out; remaining parser input increments `byteCount` **before** `gps.encode()` validates a complete NMEA sentence. Listen tests only `o.bytes != baseBytes`. One stray character or partial sentence is sufficient to fail.

The pre-test GPS status did prove fresh checksum-valid NMEA at 38400, but that occurred before the request and is not evidence of complete sentences during Listen. The printed wording `NMEA kept flowing; receiver did not enter standby` is an interpretation hard-coded into the reporter, not a checksum result from this window. Actual possibilities include continued output, early receiver wake, a restart-related character/event or unrelated non-UBX data. The stored count `listenBytes` is not printed in this failure report and was not captured elsewhere.

### 5. Does the firmware drain the UART correctly?

**The old software ring is cleared; complete receive cleanliness is not demonstrated.**

- While awaiting TX completion, normal foreground parsing continues, bounded to 64 bytes per `pollUart()` call. There is no separate RX-drain barrier immediately before taking the silence baseline.
- BSP `Uart::end()` disables IRQs, stops RX/TX, waits for stop events, disables/disconnects the peripheral, and calls `rxBuffer.clear()`. Thus it is incorrect to claim that the entire old software RX buffer simply survives the 3-second dwell.
- `Uart::begin()` restarts one-byte RX DMA and interrupts. It clears ENDTX but does **not explicitly clear ENDRX**. Its IRQ handler can store `rxRcv` when ENDRX/RXD.AMOUNT indicate data. Whether a residual peripheral event/byte is actually delivered after this stop/start cannot be determined from the saved log. The hardware register state was not captured.
- The baseline precedes Resume and counts all subsequent non-UBX parser input; no fresh receive epoch, classified drain or raw-byte record isolates restart effects. Normal NMEA parser state is not reconstructed on resume, although `receivedValid` is reset.

Clearing the software ring is therefore a real safeguard, but insufficient evidence to classify every post-restart byte as new complete receiver output. A blind extra discard window would also risk hiding real traffic; the missing bytes/events should be measured rather than silently ignored to obtain PASS.

### 6. Command/timing/wake-source comparison with u-blox requirements

The [SPG 5.10 interface description, UBX-21035062 R03, §3.16.6.1, pp.116–117](https://content.u-blox.com/sites/default/files/u-blox-M10-SPG-5.10_InterfaceDescription_UBX-21035062.pdf) defines a 16-byte version-0 PMREQ payload, backup at bit 1, force at bit 2, and duration zero for pin-triggered wake. UART RX wake is bit 3 and responds to either edge. Consequently the source frame layout and `0x08` UART wake selection match that protocol; `0xE4` is not an equivalent UART-RX mask under this definition.

The [MAX-M10S integration manual, UBX-20053088 R04, §3.6.3.2, p.42](https://content.u-blox.com/sites/default/files/MAX-M10S_IntegrationManual_UBX-20053088.pdf) **requires force for software standby**. The approved source omits it. Its comment/test assumption that force is USB-only is wrong for this target. This is the clearest documented mismatch and the most likely reason the requested low-power state was not established.

The reviewed sections do not prescribe this test's particular 3000/1500 ms windows; those are application choices, not module guarantees. A longer delay does not correct the mismatched command. Because an edge on the selected RX signal can wake the module, disconnecting/reconnecting MCU UART ownership before checking silence is also a potential disturbance. The source intends D6 idle HIGH, but actual line transitions were not measured. The test cannot distinguish never entering standby from entering and waking before Listen.

### 7. Could breakout power/backup wiring prevent software standby?

The module manual requires VCC and V_IO to stay supplied in software standby, permits unused V_BCKP to remain open, and distinguishes this from hardware backup requiring switched main rails and retained backup supply. It also warns that software standby loses RAM configuration. These requirements do not establish the wiring of this particular breakout. The known harness is only V→3V3, G→GND, GPS TX→D7 and RX→D6; no breakout schematic or module-rail measurements were saved. Missing an external backup cell alone is not evidence of a software-standby wiring fault. Keeping USB/main power present is not itself contrary to this operating mode. [Module manual](https://content.u-blox.com/sites/default/files/MAX-M10S_IntegrationManual_UBX-20053088.pdf).

Board-level VCC/V_IO/V_BCKP connections, regulator/level-shifter behavior, RX idle level and reset/EXTINT pulls remain unverified. Such wiring or spurious wake edges could interfere, but no specific board fault is demonstrated. Existing successful bidirectional UBX/NMEA communication is evidence that the active-mode link works; it does not qualify backup behavior. No schematic was guessed and no physical wiring/power action was performed.

## Smallest safe corrective action — proposed only

1. Correct the supervised PMREQ flags from `0x02` to `0x06` (backup plus the additional documented bit), preserve `duration=0` and `wakeMask=0x08`, and recompute the checksum. Update the corresponding incorrect golden-frame/comment expectation when a firmware change is authorized. No arbitrary wake-mask change or weakening of recording/entry guards is justified.
2. For a trustworthy later hardware test, capture bounded, timestamped raw RX plus byte/sentence/checksum counts around request, park, resume and Listen; log TX completion versus its fallback and relevant RX-event state. Report `unexpected UART bytes` unless valid NMEA was actually decoded. This is proposed instrumentation, not a reconstructed trace or an applied change.
3. Confirm module power/wake prerequisites and a permitted recovery plan before exercising genuine standby. Since retained baud/profile behavior is not proven, verify the ability to regain communication before assuming the existing 38400-only audit can restore the post-standby configuration. No new BBR/flash/OTP settings, reset or recovery procedure is authorized here.

The recalculated *proposed*, untransmitted frame would be:

```text
B5 62 02 41 10 00
00 00 00 00 00 00 00 00 06 00 00 00 08 00 00 00
61 6B
```

The existing fail-closed result remains valid: this cycle did not establish standby. The exact physical traffic source and complete command delivery remain unknown. The minimal protocol correction is warranted by the documentation, but cannot be claimed to have cured hardware until a separately approved, adequately instrumented test succeeds.

**No corrective action was applied. Firmware source, tests and approved artifacts remain unchanged. The device was not contacted, reset, uploaded or tested during this investigation.**


## Authorized local correction and new candidate (2026-11-03)

This follow-up supersedes the earlier "proposed only" status for local source changes. The original uploaded binary, failed-cycle transcript and investigation remain historical evidence; no new physical test or device connection was performed.

The module-specific requirement was rechecked against MAX-M10S Integration Manual UBX-20053088 R04, section 3.6.3.2 (p.42): software standby requires force, with VCC and V_IO supplied. M10 SPG 5.10 Interface Description UBX-21035062 R03, section 3.16.6.1, defines backup bit 1, force bit 2 and UART RX wake bit 3 (either edge). The corrected 24-byte request is:

```text
B5 62 02 41 10 00 00 00 00 00 00 00 00 00 06 00 00 00 08 00 00 00 61 6B
```

Only the shared command flags changed to backup | force. Duration zero, wake mask 0x08, existing TX drain/park/listen timing, wake retries, recorder admission checks, recovery verification and failed-recovery latches remain as before. Automatic standby remains OFF in the selected candidate; the automatic environment is a compile check only. No baud scanning, reset, persistent receiver configuration or recording/flash write was added.

New diagnostic observations are enabled only with GPS BACKUP TEST. A fresh independent checksum parser starts at each phase boundary, separating complete checksum-valid sentences from checksum failures and partial/unclassified bytes. UART pending counts at test start and immediately before UART end expose queued input that the BSP clears. This does not prove where a later fragment originated. The existing decision still stops entry on the first observed non-UBX byte-counter change; this was not extended into a second collection period. Therefore a sentence longer than the observed prefix can remain unclassified, and the trace must not be described as a complete 1.5-second capture when failure occurred earlier.

A bounded RAM trace retains the first 96 raw bytes separately for Drain, Listen and Wake, each with a 32-bit timestamp relative to test start. All active phases retain total byte, valid-sentence and checksum-failure counts plus first/last read times. Up to 24 action/phase transitions record time and TX-ready observations, showing request, park, resume, wake retries, baud confirmation and profile recovery. Recorded read times are not on-wire/IRQ arrival times. TX-ready at Park distinguishes normal drain from its existing fallback, but does not establish receiver acknowledgement or acceptance. Raw output states captured versus total bytes; truncation does not stop counters. There is no allocation, waiting or USB diagnostic output in the UART observation path. The buffered report prints after the cycle decision.

The report now says RX activity / entry unconfirmed rather than claiming NMEA kept flowing. No complete valid sentence means partial/residual/unclassified, not proof of buffered residual bytes. Silence and deliberate wake are reported separately; valid NMEA at 38400 and verified 10 Hz remain necessary for PASS.

Exact edited implementation/test files:

- `src/gps_power.h`: documented force bit and corrected flags; wake mask retained.
- `src/gps_receiver.cpp`: bounded observation hook, deferred diagnostics and accurate failure wording; admission/recovery predicates unchanged.
- `src/gps_backup_trace.h`: new fixed-capacity trace, per-phase counters and timestamps.
- `tests/reusable/gps_backup_test.cpp`: independent full-frame oracle, bounded trace/reset/wrap checks, existing state-machine safety cases retained.
- `tests/reusable/gps_backup_integration.cpp`: independent command oracle in actual receiver glue; three new trace classification cases; successful cycle trace checks.
- `tests/reusable/gps_backup_tests.py`: run ten integration cases.
- `tests/reusable/standby.cpp`: correct full-frame expectation and erroneous USB-only assertion.
- `tests/reusable/standby_tests.py`: correct obsolete force-bit comment.

Evidence and candidate files: `logs/hardware-gps-backup-2026-11-03/force-backup-fix/`. Previous build outputs were preserved before rebuilding. New build artifacts and changed source snapshots are copied into this directory; the new manifest hashes project firmware inputs and archived ELF/HEX/ZIP/BIN outputs rather than relying on mutable build-directory paths.

Candidate: `tacticpod_reusable_usb_gpsbackup`, application BIN 209536 bytes.
SHA-256: `f6dcdf4ca6a3617153cecea7eff977327d8c932948e7912da1743c7103ba1434`.
This differs from the previously approved/uploaded aced1874... candidate and has no upload approval.

Validation: all four final builds PASS; ten focused receiver integration cases PASS; linked startup checks PASS; negative controls reject both missing force (full-frame oracle failure 161) and the original circular recovery gate (recovery integration failure 11). Complete 32-suite run PASS: 32/32 suites, exit code 0; all ten receiver integration cases pass in the complete run. Full transcript: `force-backup-fix/regressions.txt`; individual logs and machine-readable results: `force-backup-fix/regression-logs/` and `regression-summary.json`.

Selected build resources: RAM 125008 / 237568 bytes (52.6%); flash 209536 / 811008 bytes (25.9%). Diagnostic state adds about 3 KiB of static RAM compared with the reusable build. Timing impact is bounded by one additional checksum parse/counter update per byte and up to 96 stored samples in each captured phase; no physical timing measurement is claimed.

Hardware limitations remain: software standby clears receiver RAM configuration, so retained baud/profile behavior and the ability to regain communication must be checked in the supervised recovery plan. This patch deliberately preserves the 38400-only recovery path; a receiver returning at another baud cannot produce PASS and recording remains gated. Breakout power and wake-line behavior remain unverified. Primary documentation warrants the protocol correction, but successful physical entry/wake has not been established. No upload, GPS BACKUP TEST rerun, reset, automatic standby enablement, erase, recording, merge, push, tag or release was performed.

Final offline verification: all source, firmware-input and 16 archived artifact hashes match the new manifest. Trace reset clears counts in place without a multi-KiB stack temporary; linked command prologue uses 232 local stack bytes plus saved registers (not a whole-firmware peak guarantee). Previous candidate outputs are preserved. No physical device operation occurred.


## Approved force-bit candidate: preflight stopped before upload (2026-11-03)

The owner explicitly approved one upload and exactly one supervised GPS BACKUP TEST of application SHA-256 `f6dcdf4ca6a3617153cecea7eff977327d8c932948e7912da1743c7103ba1434` (209536 bytes). No upload or test cycle was performed because the existing firmware's GPS communication readiness check failed during preflight.

Verified before stopping:

- The archived application size and SHA-256 exactly match approval and the new manifest. All recorded project source/test/framework/artifact hashes match. The application extracted from the DFU ZIP and ELF matches the same approved binary.
- The package contains only an application, requires existing S140 0x0123, and uses the existing Adafruit serial DFU bootloader path. The planned upload writes internal MCU application flash and does not intentionally erase external SPI recordings. The upload procedure was not invoked.
- COM8 identifies as USB 2886:8044, unique serial `0E8A89B7289430BD`, matching the previously confirmed XIAO nRF52840 Sense.
- Existing firmware communicates at 115200. RECORD STATUS is STOPPED; OBS capture and fast IMU are OFF; COMP capture is 0 with both queues empty; TCB is idle state 1, session 0, lock 0, error none, programs 0. No framed export was present, and normal console responses establish that the TCB export guard is not suppressing commands. No recording/export/capture was initiated.

The additional GPS readiness check, performed before DIAG ID/upload, returned:

```text
GPS STATUS | UART=Serial1 RX:D7 TX:D6 38400 | NMEA=NONE | bytes=16391 | checksum_ok=228 | checksum_fail=0 | last_valid_message_age_ms=N/A
GPS configuration: UNVERIFIED (no receiver communication after wake/start)
GPS ERROR: UART data stopped; check power and TX wiring.
```

The historical 10 Hz profile line is not current verification. Cached position age was 2054021 ms; historical checksum-valid sentence counts do not establish current communication. The cause of the present UART silence is undetermined: no scan, wake, reset, power cycle or further diagnostic was attempted.

Following the stop condition, serial was closed immediately. Fresh JEDEC/SFDP identification was **not reached**, so external flash communication has not been re-verified during this attempt; there is no new flash-failure finding. There was no bootloader-entry request, DFU process, firmware upload, GPS BACKUP TEST command, additional cycle, erase, recording, automatic standby enablement, firmware modification, merge, push, tag or release.

Evidence: `logs/hardware-gps-backup-2026-11-03/hardware-run-force-approved-one-20261103/` contains `candidate-verification.json`, `verified-elf.bin`, complete `pre-upload-serial.txt`, `preflight-result.json` and `hardware-summary.json`. The new host orchestration script is `logs/hardware-gps-backup-2026-11-03/approved_force_one_cycle.py`; it was invoked only with `prepare`. No upload/cycle attempt markers exist.

Outcome: **STOPPED BEFORE UPLOAD; zero supervised cycles.** The candidate remains unchanged. GPS communication and a fresh complete preflight must be established before this candidate can be tested; no further action was taken.


## Resumed approved force-bit candidate: one upload, one failed cycle (2026-11-03)

This supersedes the earlier preflight-only stop for the subsequent, explicitly authorized attempt. Following the owner's full power cycle, fresh preflight passed and the exact approved application was uploaded once. Exactly one supervised GPS BACKUP TEST ran and failed at backup-entry/silence confirmation. Serial was closed immediately after its complete buffered report. No further device commands or recovery actions followed.

### Identity, application and preflight

- COM8, USB 2886:8044, serial `0E8A89B7289430BD`: matches the confirmed XIAO nRF52840 Sense. Same unique serial in the expected 2886:0045 bootloader on COM7; application returned on COM8.
- Approved application: **209536 bytes**, SHA-256 **`f6dcdf4ca6a3617153cecea7eff977327d8c932948e7912da1743c7103ba1434`**. Manifest/source/framework/artifact hashes verified immediately before upload; ELF extraction and ZIP application bytes match. DFU ZIP SHA-256 `f2c116fa73d0993f0dfed69bfaecc787ecf92081690d3c4aa157ee0b4e981140`.
- Recording stopped; OBS/fast IMU OFF; COMP capture off and queues empty; TCB idle, unlocked, no error. No active framed export or suppressed status replies. GPS NMEA valid at 38400 and current RAM readback verified at 100ms / RMC 10Hz / GGA 1Hz, rechecked at the end of preflight.
- Fresh DIAG ID passed: five `0B4018` responses; both complete 256-byte SFDP passes match the saved reference, header `53464450000101FF`; SR1 before/after `00`, SR2 `02`. `qualification=UNRESOLVED` was treated as informational.
- One application-only Adafruit serial DFU through the existing verified bootloader, `--singlebank`, existing S140 requirement 0x0123. Exit code 0, "Activating new firmware / Device programmed." No intentional external SPI erase; no bootloader or SoftDevice package supplied.

### Startup and single-cycle result

Startup/menu and serial at 115200 passed. Fresh post-upload flash identification/SFDP passed with the same values. Recorder/capture status was idle. GPS STATUS reported current checksum-valid NMEA at 38400 and verified 10 Hz RAM configuration before the one test command was issued.

Cycle result: **FAIL / EntryNotConfirmed**. The command was reported queued as 24 bytes with flags 0x06 and wake mask 0x08. No PMREQ acknowledgement was expected. The transmit-ready observation at Park was true; this is MCU-side evidence, not independent receiver acceptance.

All times below are host UART read/action observations, in milliseconds relative to test start, not physical wire/interrupt arrival timestamps:

| Event | Time | Observation |
| --- | ---: | --- |
| PMREQ request action | 1 | Request queued; TX ready before write |
| UART Park | 7 | TX ready true at Park; pending RX count 1 |
| UART Resume / Listen begins | 3007 | Full 3000 ms parked dwell elapsed |
| First Listen byte | 3007 | One byte `0x32`, ASCII `2` |
| Entry failure / Done | 3007 | First-byte activity rejected silence; no wake action |

The requested 1500 ms quiet interval was **not completed**: failure occurred in the same millisecond as resume. The Listen phase captured exactly one byte, zero complete valid NMEA sentences and zero checksum failures. This does not establish continued NMEA streaming, failed physical entry, or a failed deliberate wake. Residual/stale peripheral data or a restart transition remains plausible; a byte's residual origin and an early physical wake cannot be proven from this trace alone.

The Drain phase recorded 26 bytes from 1 through 7 ms, all saved, with no complete valid sentence or checksum failure. Its ASCII prefix is `.60,A,5046.51467,N,00005.3`. Queued RX counts at start/park were 1/1. The BSP clears the software RX ring at UART end; these counts and the immediate single-byte resume observation are useful evidence but do not prove which hardware/software source produced that byte.

There were **zero deliberate wake attempts**, zero Wake-phase bytes and no Profile/Fix phase execution. Post-cycle valid NMEA, baud recovery and 10 Hz recovery were **not confirmed**. The last verified NMEA/10 Hz/flash evidence is from before this cycle. No additional GPS STATUS, DIAG ID, baud scan, reset, power cycle, recording admission probe or flash export was performed after the failed result. Current recording readiness and post-failure flash communication remain unverified; no claim of a failed wake or successful recovery is made.

### Evidence and stop

Full preflight transcript: `logs/hardware-gps-backup-2026-11-03/hardware-run-force-resumed-one-20261103/pre-upload-serial.txt`.

Full post-upload startup/checks and one-cycle transcript, including all buffered timestamped diagnostics: `post-upload-one-cycle-serial.txt` in that directory. The complete cycle report is also saved as `cycle-1-response.txt`. Supporting records: `candidate-verification.json`, `preflight-result.json`, `upload-attempted.json`, `dfu-invoked-once.json`, `upload.txt`, `upload-result.json`, `cycle-command-issued.json`, `test-result.json`, parsed `hardware-summary.json` and SHA-256 `evidence-manifest.json`.

Outcome: **one successful application upload; one failed supervised cycle; immediate stop.** No additional cycles, external flash erase, recording, firmware modification, automatic standby enablement, push, merge, tag or release. Approved source and archived artifacts remain unchanged. Physical root cause remains unresolved; this result is being reported before any further action.


## Local RX-event diagnostic candidate (not uploaded)

See [gps-backup-rx-diagnostic.md](gps-backup-rx-diagnostic.md) for the exact changes, fixed-capacity register snapshots, sticky-failure 1500 ms receive-only observation, unchanged safety gates and instrumentation limits. New BIN: 211648 bytes, SHA-256 `56d9fb3539fd2b164f432eb5c7fa557c94489b77405494fd17eb28f426b747ad`. Seven builds, all 32 regression suites, eleven receiver integration cases, both snapshot models, seven linked-vector checks, linked startup checks and all three negative controls PASS. See rx-event-diagnostic/regression-summary.json and full archived logs. No hardware operation or firmware upload occurred. The prior physical failure remains the last hardware result.
