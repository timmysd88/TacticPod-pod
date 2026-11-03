# External SPI flash detection: read-only investigation

Date: 2026-11-03. **Uploads and GPS backup testing remain blocked by the owner's instruction. No firmware changes, reset, wake command, programming, status writes or erases were performed.**

## Conclusion

No changed SPI pin mapping, JEDEC/SFDP framing, boot release or standby-exit release was found compared with the previously working firmware. The strongest supported conclusion is a **runtime flash/SPI communication or power-state failure**, not a demonstrated regression in PR #6 or the local GPS correction. That candidate has never been uploaded.

The leading *software* hypothesis is a flash left in deep power-down, or a release transaction that was ineffective at the device. The diagnostic assumes boot/wake already released the chip: it neither issues its own release nor verifies readiness. This is a real defensive gap, but **not a proven physical root cause**. The same code previously worked and normally releases the flash correctly. A CS/clock/driver, supply/contact or HOLD/reset issue remains possible; all-zero reads alone cannot distinguish them.

## Previously working baseline

The recorded successful application upload was reusable firmware `155a999122f0ce85ab6817c6eba690e9501fe4b3`, BIN SHA-256 `40bced328dbcc453eb8326297fabab40c268063140d6c909a4359b4024a9a585`. The original workspace's `logs/field-retest-2026-11-02/post-upload-result.json` records:

- Five `0B4018` JEDEC results; SR1 `00`, SR2 `02`, REMS `0B17`.
- Valid repeated SFDP and complete session downloads.
- **The successful diagnostic also printed `qualification=UNRESOLVED`.**

The later physical event/report records standby entry and USB wake followed by successful readback. Thus neither standby support nor the release helper is inherently incapable of working on this board. This is historical evidence, not proof of the current flash's state.

Source comparison confirms the following function bodies are identical between that working commit and the current candidate: `FlashIo`, `enterStandby`, `leaveStandby`, `readFlashJedecId`, `readSfdp`, `identityRead`, `identityIdle`, `updateIdentity`, and `requestIdentity`. The complete flash-power helper, identity reader, recorder/SPI session implementation and project variant files are also unchanged. Source fingerprints and comparisons are saved in `logs/hardware-gps-backup-2026-11-03/flash-detection-investigation/source-comparison.json`.

Startup LED changes and the slower idle button poll are present in later commits, but do not change the traced SPI initialization/release order. The local GPS recovery correction changes neither these paths nor SPI mapping. The console has no authenticated running-image build-ID command, so the actual installed image's exact SHA was not read from hardware; `155a999` is the last documented successfully uploaded working image, not a newly read device firmware identity.

## Requested checks

| Check | Source finding | Assessment |
| --- | --- | --- |
| Release before identification | Reusable `setup()` preloads CS HIGH, configures CS OUTPUT, calls `SPI.begin()`, then `FlashPower::release()`. The helper sends `0xAB` under one CS assertion, deasserts CS and waits 100 microseconds. IMU initialization follows, then the read-only recorder scan; diagnostics run in `loop()` after setup. | Boot does attempt release before any identification. There is no device acknowledgment/readiness validation of that release. |
| SPI pins | D2 = P0.28, software CS; D8 = P1.13, SCK; D9 = P1.14, MISO; D10 = P1.15, MOSI. Variant definitions match the working image. The core constructs SPI with these pins and uses SPIM3. | No mapping regression. Variant `SS=7` is not used for automatic chip select: the core sets hardware SS unused, and application code controls D2. |
| Initialization and CS | CS latch is set HIGH before OUTPUT mode; `SPI.begin()` precedes transfers. All flash transactions use mode 0, MSB first, 1 MHz. Each begins the SPI transaction before CS LOW and ends with CS HIGH before `endTransaction()`. `9F` is followed by exactly three read bytes under one CS; `5A` has a 24-bit address and one dummy byte. | No framing or initialization-order regression found. The application never calls `SPI.end()` or remaps SPI pins, so normal standby exit does not need to re-run `SPI.begin()`. The IMU uses I2C/Wire1, not this SPI interface. Actual electrical levels/clocks were not measured. |
| Standby and wake | Entry sends `0xB9` only if SR1 is not `FF` and WIP/WEL are clear. USB VBUS or an open serial port blocks standby. The Exit transition calls `leaveStandby()`, which sends `0xAB`/100 us before GPS/IMU resume; a button start also wakes peripherals before requesting a session. | No missing release in the normal exit path. However, release is unacknowledged; an ineffective transfer is not detected or retried. SR1 `00` alone cannot distinguish an awake idle device from an all-zero unresponsive path. |
| Diagnostic readiness/timing | `identityIdle()` checks recorder/session/GPS/fast-IMU/compressor ownership, not flash power/readiness. `identityReader.begin()` zeroes its snapshot; the first transaction is SR1, then JEDEC. Neither it nor the legacy `DIAG` reader sends `0xAB`. | A flash that remains asleep can be interrogated without another release attempt. Diagnostics were requested many minutes into the boot (saved GPS timestamps exceed 2.4 million ms), so an ordinary immediately-after-reset timing race does not fit the observed run. Late/unstable external supply or a failed earlier release cannot be ruled out without measurement. |

The [manufacturer-authored XT25F128F Rev 1.2 datasheet](https://cdn.semikey.com/upload/pdfs/7f/97/7f97109b0187d7aa9ee7c57f038dfe1c.pdf), section 5.3.9 and AC tables, describes release as `AB` followed by CS HIGH and a 20 us recovery interval. The existing 100 us wait exceeds that candidate-family value; there is no evidence supporting a larger arbitrary delay as the fix. This does not identify the installed chip as the F revision: `0B4018` and existing SFDP evidence do not establish exact B/F provenance. Actual CS-high duration and delivery of `AB` at the chip remain unmeasured. The B datasheet mirror could not be fetched during this investigation; no new B-specific timing claim is made.

## Read-only hardware observation

An additional 115200-baud session queried only `RECORD STATUS`, `OBS STATUS`, `COMP STATUS`, `TCB STATUS`, `BAT STATUS` and legacy `DIAG`. Recorder/capture states were idle and `usb_vbus=1`.

Legacy `DIAG` independently reported five `00 00 00` IDs and an all-zero SFDP header, ending with `SFDP INVALID / unavailable: missing SFDP signature`. This agrees with both earlier raw `DIAG ID` captures. The fault is therefore not isolated to the newer identity reader's snapshot buffer or report formatter.

The host collector waited for the healthy-SFDP summary marker, which the firmware intentionally omits on invalid SFDP. Its 15-second marker timeout is a collector limitation, **not evidence that an SPI transfer itself timed out**. The complete invalid-SFDP reply is saved; the port was closed and no subsequent commands were sent. No new `DIAG ID` was issued in this session because the collector stopped at that marker; comparison uses the earlier complete raw captures.

Evidence: `logs/hardware-gps-backup-2026-11-03/flash-detection-investigation/readonly-serial.txt`, `readonly-result.json` and `source-comparison.json`. No RAM event history was drained. No attempt was made to reset the MCU, toggle power, wake the flash, alter registers or read/start/erase recording data in this investigation.

## Host preflight correction required

The added check in `hardware_run.py` that rejects `qualification=UNRESOLVED` is **incorrect**. `src/main.cpp` unconditionally prints that literal in every completed raw identity report, and the known-good hardware log contains it. It is a provenance/qualification caveat, not an operational pass/fail signal. Removing this rejection will not fix the observed zero IDs: the expected-ID check already stops first.

Before using that host preflight again, remove the unconditional qualification rejection. Require the actual expected repeated JEDEC ID, a valid SFDP signature/structure and byte-consistent data against the previously saved device reference instead. Retain WIP/WEL, ownership, capture and session-preservation checks. **No host preflight or firmware code was changed as part of this read-only investigation.** Upload remains blocked regardless.

## Smallest safe corrective action proposed

First distinguish the leading software hypothesis with one explicit, idle-only **wake-then-identify** operation: keep D2 CS HIGH, use the established SPI pins/mode/clock, send the existing `0xAB` release, keep CS HIGH for the existing 100 us, then read JEDEC/SFDP and require valid results. Do not send reset sequences, WREN, status writes, program or erase commands. `AB` changes power state, although it does not program/erase the array; it was therefore **not performed under this read-only authorization**.

The smallest firmware change, if later authorized and warranted, is to call the existing `FlashPower::release()` immediately before identity capture under the existing idle/ownership guard, followed by fail-closed validation. Because `DIAG ID` is currently documented as issuing only read opcodes, an explicitly named wake-then-identify wrapper can preserve that strict read-only contract rather than silently changing it. Do not expand the generic identity reader's opcode allowlist or weaken recording admission.

If the single wake-then-read restores `0B4018` and the original SFDP, that supports a missed/ineffective wake hypothesis and justifies the defensive wake preparation. If it still returns zeros, stop: changing delays or pin mappings without evidence is not justified. The next discriminator is passive observation of D2/SCK/MOSI/MISO and the module supply/HOLD/reset levels during the transaction. Seeing `AB`, CS HIGH for the recovery interval and `9F` at the chip, but no response, separates a receiver/module/electrical fault from a missing MCU transaction. No such measurement has been made here.

No corrective firmware action has been applied. Existing sessions remain unreadable/unverified; nothing in these results demonstrates that they were erased. Approved candidate source and artifact hashes remain unchanged. Uploads and GPS backup cycles remain blocked until the owner changes that instruction.

## Follow-up: explicitly authorized existing-firmware wake check

The owner authorized one idle-only `AB` wake, 100 us CS-high delay, then `9F`/`5A` readback **only if the existing firmware already exposes a safe interface**, and separately authorized correcting the host preflight.

COM8 again matched VID:PID `2886:8044`, serial `0E8A89B7289430BD`. Status-only queries confirmed V1 STOPPED; OBS/fast IMU OFF; compressor idle/no fault with empty queues; TCB state=1/error=none/lock=0. The running firmware's reconnect menu was captured as additional capability evidence. Existing SPI source mapping/settings remain CS D2/P0.28, SCK D8/P1.13, MISO D9/P1.14, MOSI D10/P1.15; mode 0, MSB first, 1 MHz.

**Wake diagnostic BLOCKED: no existing safe console command exposes `0xAB`.** Both available flash diagnostics issue only read opcodes. Release remains internal to setup and standby exit; the identity reader's allowlist explicitly excludes AB. No guessed console command, raw SPI injection, standby trigger, power cycle, reset, upload or firmware modification was used to get around this limitation. The port was closed. Zero wake attempts were made, so there are no after-wake JEDEC/SFDP responses and no claim that communication was restored. The last actual identification evidence remains five zero IDs and invalid/all-zero SFDP, as recorded above.

**Host preflight correction COMPLETE.** The earlier section describes the historical erroneous check; it has now been replaced, under the owner's explicit instruction, in `logs/hardware-gps-backup-2026-11-03/hardware_run.py`. The new `validate_flash_identity()` ignores qualification text for admission and requires:

- Complete capture markers and no identity error.
- Five expected, ordered `0B4018` results and successful repetition flags.
- The previously verified idle/protection state, SR1 before/after `00`, SR2 `02`.
- Two complete, ordered 256-byte SFDP passes; each actual chunk must match the saved verified device reference, whose header must contain `SFDP`.

Offline validation passed **11/11 checks**: accepts the genuine known-good `qualification=UNRESOLVED` capture and an alternative informational label; rejects the actual all-zero failed capture even with repetition PASS, inconsistent/all-FF IDs, busy/changed status, missing/corrupt SFDP, incomplete capture and an invalid reference. No upload or device access occurred in those tests. Firmware source/test/build artifact hashes still match the approved candidate; no rebuild was needed for this host-only correction.

Evidence: `logs/hardware-gps-backup-2026-11-03/flash-wake-capability-check/` contains the complete `status-only-serial.txt`, `capability-result.json` and `host-preflight-test-results.json`. Reproducible offline checks: `test_host_flash_preflight.py` beside the host preflight script.

Uploads and GPS backup tests remain blocked. Because no supported wake test can be performed and the last physical reads were unresponsive, the next action is physical supply-voltage and continuity/connection checks at the flash, including CS, SCK, DI/DO and HOLD/reset levels, before further firmware changes. None of those electrical measurements has been claimed or performed here.

## Later read-only DIAG ID recheck

The owner's subsequent explicit request to run `DIAG ID` at 115200 produced a **PASS** on COM8 after an initial refusal during a GPS profile check. All five JEDEC reads returned `0B4018`, SR1 before/after `00`, SR2 `02`, and SFDP header `53464450000101FF`. Both full 256-byte SFDP passes matched the previous reference byte-for-byte. Exact output is saved in `logs/hardware-gps-backup-2026-11-03/diag-id-recheck-20261103-091015/serial-transcript.txt` and `result.json`.

The host issued no flash wake, reset, upload or write/erase command. The first attempt's transcript also captured fresh profile and automatic legacy identification output, but the reason for restored communication is not established. This result supersedes the earlier current-communication failure, while preserving its history; it does not prove a specific root cause or session integrity. Uploads and GPS backup cycles remain blocked by instruction.


## Final owner-directed read-only preflight after full power cycle

The owner subsequently confirmed a complete USB-and-battery power cycle. A final read-only preflight passed: five `0B4018` IDs, SR1 `00`, SR2 `02`, two full SFDP passes matching the reference, idle recorders before/after, a CRC-valid recording export, and the complete older 647,168-byte recording prefix preserved byte-for-byte. The historical interrupted session 133 remains unchanged; complete session 159 was already present before this preflight, with no new integrity issues. The corrected candidate source/patch/BIN/ZIP hashes remain unchanged. Evidence and exact checksums are in `hardware-gps-backup-test-report.md` and `logs/hardware-gps-backup-2026-11-03/final-readonly-preflight-20261103-091526/`.

**READY FOR FLASH APPROVAL**, then stopped as requested. No upload or GPS backup test occurred. Further explicit approval is required before proceeding. Power-cycle recovery is now owner-reported fact; the particular electrical or retained-state cause remains unproven.


## Subsequent explicitly approved upload

The corrected supervised GPS test application was subsequently uploaded once under fresh explicit approval. Flash identification/SFDP matched the verified reference both before upload and at application startup. Thus the earlier external-flash communication failure was not reproduced in those checks. The first physical GPS backup cycle then failed its UART-silence/standby-confirmation check, before an explicit wake attempt; testing stopped immediately. No further flash diagnostics or session export were issued after that failure. See the hardware test report and `logs/hardware-gps-backup-2026-11-03/hardware-run-approved-20261103/` for the full transcript. This does not establish a causal relationship between the earlier flash fault and the GPS standby failure.
