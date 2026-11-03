# GPS backup hardware test report

Latest attempt (2026-11-03): approved diagnostic candidate preflight STOPPED on GPS NMEA=NONE/current profile UNVERIFIED. No upload or cycle; see the preflight-stop record at the end.

Latest hardware result (2026-11-03): the approved force-bit candidate was uploaded once; its one supervised cycle FAILED entry confirmation on a single 0x32 byte immediately after UART resume. No deliberate wake or further cycle; serial closed. See the resumed attempt at the end.

Latest hardware attempt (2026-11-03): preflight STOPPED before upload because the existing GPS reports NMEA=NONE and UART data stopped. Zero new uploads or test cycles; see the approved force-bit candidate attempt at the end.

Latest status (2026-11-03): the authorized local force-bit correction has passed software validation. The new candidate is not uploaded or hardware-verified. See the authorized local correction follow-up below for its checksum, complete file list and results.

Date: 2026-11-03 (Europe/London). **Approved corrected firmware uploaded successfully. Physical GPS backup cycle 1 FAIL: standby was not confirmed; testing stopped immediately.**

## Repository and source provenance — PASS

- Repository: `timmysd88/TacticPod-pod`, origin `https://github.com/timmysd88/TacticPod-pod.git`.
- Original working branch remains `main` at `155a999122f0ce85ab6817c6eba690e9501fe4b3`. Its modified results file and 3,236 pre-existing untracked log files were preserved. It was never reset, stashed or checked out over.
- `git fetch origin --prune` and `git fetch origin pull/6/head` succeeded. Connected [GitHub PR #6 metadata](https://github.com/timmysd88/TacticPod-pod/pull/6) and the fetched head both verified **`29f90966d868f68b85d63d64d27bab9584f7628a`**, open and unmerged. Metadata is saved in `logs/hardware-gps-backup-2026-11-03/github-pr6-metadata.json`.
- Active testing worktree: `.p6`, branch `test/pr6-gpsbackup-short-20261103`. Clean at creation; now intentionally dirty with this uncommitted correction, tests and report artifacts. The longer initial worktree remains preserved; it exceeded Windows compiler path limits.
- **Candidate identity: base commit `29f90966d868f68b85d63d64d27bab9584f7628a` plus the uncommitted patch** in `logs/hardware-gps-backup-2026-11-03/recovery-gate-fix/recovery-gate.patch`. This candidate is not the unchanged PR head and has no new commit SHA. Its patch SHA-256, per-file hashes, flags and build hashes are in `recovery-gate-fix/manifest.json`.
- No commit, merge, tag, push, publication, firmware upload or automatic-backup activation was performed.

The original report is preserved at `recovery-gate-fix/baseline-report.md`. Original firmware artifacts, manifests, initial failure evidence and qualified baseline reruns are preserved separately from the corrected results.

## Cause and correction

**Original source failure:** `backupStep()` supplied `recordingReady()` as the profile-recovery observation. In the test build, `recordingReady()` requires `!backupTestRunning()`, but the Profile phase remains running until it sees recovery. The test could therefore never advance to PASS after a successful wake and verified RAM profile; it failed or hit the 45-second profile timeout. The old portable unit test injected a successful observation directly and missed the receiver integration error.

**Correction:** a test-only `backupRecoveryVerified()` observes fresh checksum-valid NMEA (within the existing 5-second freshness window), a listening UART, completed startup/profile work, no profile failure and `GpsProfile::currentVerified()`. It depends on neither recording admission nor the failure latch. This observation advances the backup test and clears a failure latch only after genuine recovery. The existing recording admission guards still require `!backupLatched && !backupTestRunning()`; `prepareRecording()` still refuses during the test.

The state machine now gives an explicit profile failure precedence over a stale successful observation, requires valid NMEA as well as profile verification to finish the Profile phase, and fails if NMEA freshness/profile verification is lost during the optional fix-status window. A fix remains optional, as before; NMEA and the verified 10 Hz profile remain mandatory. Failure continues to block recording.

Unchanged: GPS wake mask, UART baud, request/wake byte sequence, retry count/timing, profile/fix deadlines, entry safety guards, storage, recording format, button/LED logic, battery policy, standby policy and default-off automatic-backup flag.

### Exact functional files changed

1. `src/gps_receiver.cpp`: independent recovery observation, fresh NMEA and safe latch clearing.
2. `src/gps_power.h`: require continuing recovery through the final window; fail closed on conflicting/failed observations.
3. `tests/reusable/gps_backup_test.cpp`: timeout, failed observation, final-window loss and subsequent-cycle regressions. Removed the unnecessary stdio PASS print; the Python runner prints results, so the ARM fallback no longer needs special syscall stubs.
4. `tests/reusable/gps_backup_tests.py`: safety/dependency checks and integration-test runner.
5. `tests/reusable/gps_backup_integration.cpp` (new): real receiver/parser/profile/recording-guard integration with simulated hardware/replies.
6. `docs/hardware-gps-backup-test-report.md` (this report).

Additional evidence files live under `logs/hardware-gps-backup-2026-11-03/recovery-gate-fix/`; they are not firmware changes.

## Regression tests

The focused suite passed without linker adapters. New integration cases execute production receiver/parser/profile and recorder guards and cover:

- Three successful cycles on the same simulated receiver; no recording admission while a cycle runs; admission restored after each PASS.
- No wake: bounded eight attempts, failed result, recording block and refusal to repeat before recovery.
- Bytes with invalid NMEA checksums: baud unconfirmed, no PASS and recording blocked.
- Missing 10 Hz readback replies: audit timeout, no PASS and recording blocked.
- Profile verification lost during the fix wait: no false PASS.
- NMEA becomes stale during the fix wait: no false PASS.
- Failed wake followed by independent simulated receiver recovery: real audit clears the latch; the next cycle can PASS without forcing readiness or the latch.

Portable cases additionally cover conflicting failed/verified observations, the 45-second profile deadline, the 10-second optional-fix deadline, final-window loss on the exact step that could report PASS, and success/failure/success cycles without stale results leaking between cycles. Existing request refusal, standby silence, UART restoration, abort and guard tests remain enabled.

**Negative control PASS:** replacing only the recovery observation with the original `recordingReady()` call in a generated preprocessed test unit makes the new successful-recovery integration case fail (code 11). Production/test sources were not overwritten. See `negative_control.json` and the reproducible script alongside it.

**Complete regression run: PASS — 32/32 suites, exit code 0.** The full runner completed directly on the corrected source with no special linker adapters or qualified reruns. The expanded GPS suite includes all seven receiver integration cases above. The additional linked startup test and negative control also passed. Results: `recovery-gate-fix/regression-summary.json`, full transcript `regressions.txt`, individual logs in `regression-logs/`. These are host simulations/ARM emulation, never physical hardware verification.

## Firmware builds — PASS

PlatformIO Core 6.2.0; Seeed platform `a6fd41debbbcda13b5340fff50c9092ed3289194`; ARM GCC 7.2.1; framework 1.10101.0. CI-pinned libraries: LSM6DS3 `e063c3a544b8bb89238accddf24f0028bcd1ca20`, TinyGPSPlus `ca29434514a5c5172bd807af0608df7f296582a2`. Exact committed LF checkout bytes are used for byte-exact fixture tests. Original repository configuration was not changed.

| Environment | Result | Static RAM | Flash | Use |
| --- | --- | ---: | ---: | --- |
| `seeed_xiao_nrf52840_sense` | PASS | 118,992 | 166,360 | Default regression build |
| `tacticpod_reusable_usb` | PASS | 122,040 | 202,192 | Normal reusable; automatic backup off |
| `tacticpod_reusable_usb_gpsbackup` | PASS | 122,080 | 207,808 | Selected supervised hardware-test candidate |
| `tacticpod_reusable_usb_gpsbackup_auto` | PASS | 122,096 | 208,176 | Compile check only; not approved for upload |

Selected flags: `-DTARGET_SEEED_XIAO_NRF52840_SENSE -DTACTICPOD_TCB3_FLASH_ENABLED=1 -DTACTICPOD_REUSABLE=1 -DTACTICPOD_GPS_BACKUP_TEST=1`. Automatic GPS backup remains off in the selected environment. `platformio.ini` is unchanged.

Build log: `recovery-gate-fix/build.txt`. BIN extracted from each application-only DFU ZIP, plus ZIP/HEX/ELF SHA-256 values: `recovery-gate-fix/manifest.json` and `SHA256SUMS.txt`. All ZIP manifests require existing SoftDevice `0x0123` and contain no bootloader or SoftDevice update.

## Connected hardware and flashing — BLOCKED by failed preflight

Enumerated application: **COM8**, USB VID:PID **2886:8044**, serial **0E8A89B7289430BD**. This identifies the USB device; it does not prove installed firmware, idle recorder state, baud, sensor function or session integrity. USB monitor should use **115200 baud**; GPS UART remains **38400 baud** in source.

Expected loader: existing XIAO Adafruit-compatible UF2/serial DFU. The historical 2026-11-02 `INFO_UF2.TXT` capture records UF2 0.6.1, Seeed_XIAO_nRF52840_Sense, S140 7.3.0. It has not been re-entered/verified in this run. Prior evidence used loader PID `0045`; re-identify the same physical device and actual loader port rather than assuming COM8 or a PID from the board manifest.

After explicit approval for this exact candidate, close the monitor, verify source/patch/artifact hashes, confirm idle/no export with existing non-destructive diagnostics and preserve sessions, then use 1200-baud touch or the existing double-tap RESET entry. Verify loader identity and upload the application-only ZIP with bundled Adafruit `nrfutil dfu serial -pkg <firmware.zip> -p <verified-loader-port> -b 115200 --singlebank`. Re-enumerate the application and monitor at 115200. The repository records a historical direct-PlatformIO HEX/ZIP mismatch; use the inspected ZIP uploader path. No bootloader/SoftDevice image or mass erase is involved.

If USB fails to recover, use documented double-tap RESET and inspect the loader; stop before unknown recovery actions. If GPS fails to wake, stop all further cycles. Firmware documents removing both USB and battery as the required power-cycle; that is owner intervention, not an action performed or newly verified here. Do not improvise wake masks, baud changes, resets or recovery commands.

During the correction itself no device was opened. After the owner explicitly approved this exact candidate, the read-only preflight below opened COM8 at 115200 and queried status/flash identity. No 1200-baud touch, reset, bootloader entry or upload was performed.

## Physical GPS cycles — INCONCLUSIVE

| Cycle | Request accepted | Standby silent | Wake / valid NMEA | Time to NMEA | 38400 / 10 Hz | Fix |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | Not run | Not run | Not run | Unmeasured | Unverified | Unverified |
| 2 | Not run | Not run | Not run | Unmeasured | Unverified | Unverified |
| 3 | Not run | Not run | Not run | Unmeasured | Unverified | Unverified |

No GPS backup cycle transcript exists because the run stopped before upload. The complete preflight serial transcript and captured refusal prefix are saved below. Approval has been received for the corrected candidate; once the flash communication issue is resolved, repeat the non-destructive preflight before the authorized upload. Run `GPS BACKUP TEST` only while idle/no export and fresh NMEA/10 Hz verification are established. Capture the complete serial output. Repeat only after PASS and full recovery, up to three cycles total; stop immediately on failure, incomplete recovery or unexpected behaviour.

## Recording, LED/button, battery and standby limitations

| Check | Source/host evidence | Physical status |
| --- | --- | --- |
| Recorder admission and session safety | Entry guards and failure latch preserved; new integration checks both recording admission APIs remain blocked during the test and after failed recovery; no simulated flash program/erase/write-enable permitted by these cases. | INCONCLUSIVE: real idle/export state, sessions, partition/free capacity and post-wake admission not queried. No real recording started or flash data modified. |
| IMU | Source sets sensor ODR 104 Hz, with acquisition scheduled at 100 Hz. | INCONCLUSIVE: actual registers, timing and missed samples unmeasured. |
| GPS | Source UART 38400; actual parser and independent profile model exercised by tests; real 10 Hz readback is mandatory before PASS. | INCONCLUSIVE: physical NMEA, rate, RAM profile, standby/wake and fix unmeasured. |
| Flash | Existing read-only identification/status diagnostics and bounded session partition remain unchanged. | INCONCLUSIVE: preflight identification FAILED (JEDEC 000000, all-zero SFDP, qualification UNRESOLVED); session export refused JEDEC. No recording contents could be verified. |
| Button | 25 ms debounce; 3 s start hold; stop is a tap at most 500 ms, second press within 5 s held 3 s; held-at-boot start remains blocked. | INCONCLUSIVE: no physical button/recording test. |
| RGB LED | Recording green 100 ms every 2 s; start green 1 s; clean stop two green flashes; event/fault/low-battery priority remains unchanged. | INCONCLUSIVE: no visual or timing measurement. |
| Battery | Explicit cell mode, RAM calibration, fresh valid samples and warm-up required for trust. Poll 1 s; thresholds 3600/3500/3300/3700 mV. USB mode does not infer cell presence. | INCONCLUSIVE: wiring/presence, ADC accuracy, calibration and low-voltage behaviour unverified. No mode/calibration changed. |
| Standby | System ON; VBUS/serial and active/faulted operations block entry. Settle 2 s; button polls 100/10 ms; peripherals wake before a valid start; LEDs off in standby. | INCONCLUSIVE: battery-only entry/wake and current draw unmeasured. Automatic GPS standby remains off. |

## Readiness and outcomes

**Supervised hardware test: READY at the software-validation level.** The correction, all 32 regression suites, seven recovery integration cases, negative control, four firmware builds and linked startup checks passed. The owner subsequently explicitly approved this exact candidate for supervised testing only. Upload was not attempted because the external SPI flash preflight failed. The existing approval remains scoped to this candidate; no additional firmware changes or uploads were performed after the failure.

**Pilot release:** BLOCKED. `tacticpod_reusable_usb` is the normal reusable environment, but mandatory physical checks are incomplete. No `v0.1.0-pilot` release, tag, publication or deployment was created. No production readiness is claimed.

- **PASS** — Synchronisation, all 32 regression suites, seven recovery integration cases, negative control, four builds and linked startup verification.
- **FAIL (historical, corrected locally)** — The unchanged PR head's circular recovery observation; retained in the baseline evidence.
- **INCONCLUSIVE** — All physical GPS, recorder, flash, IMU, LED/button, battery and standby checks.
- **BLOCKED** — External SPI flash did not identify/respond correctly, so upload and all three GPS cycles were stopped before flashing. Pilot preparation still requires the outstanding hardware gates.

Generated startup/bytecode outputs were copied into `recovery-gate-fix/generated-outputs/` and their tracked files restored to the base commit within `.p6`. The final content audit confirms only the four intended tracked source/test files differ, plus the new integration test/report and evidence artifacts. Original `main` work remains preserved.


## Approved hardware run: blocked before upload

The owner explicitly approved flashing the corrected `tacticpod_reusable_usb_gpsbackup` candidate to COM8 for supervised testing only. The following checks were performed before any upload:

| Check | Result |
| --- | --- |
| Device | COM8, VID:PID 2886:8044, serial 0E8A89B7289430BD; matches the previously identified XIAO Sense. No fresh loader/Sense board-ID inspection was performed because loader entry was not attempted. |
| Corrected source | PASS: base commit and all five modified source/test file hashes match the test manifest; all remaining firmware/variant/config content matches the verified PR. |
| Candidate binary | PASS: SHA-256 `aced1874981173ebfbb3904390f6088214aca3419f72f6a5fae0deb4f4c14051`. It differs from the original PR binary. ELF converted independently to BIN matches this exact package payload. The helper was inlined (no standalone helper symbol); provenance comes from the recorded build, source hashes, ELF/BIN identity and artifact manifest, not a claimed runtime helper inspection. |
| DFU ZIP | PASS: SHA-256 `91727831db63198d2c007134803526125a096adc47eeecbe1d8c3879e2004e28`; application-only, existing S140 requirement 0x0123. No bootloader/SoftDevice image and no external SPI flash erase operation are included in the planned upload. |
| Recorder/capture idle | PASS before upload: V1 STOPPED; OBS and fast IMU OFF; COMP capturing=0/fault=0, queues empty; TCB state=1, error=none, lock=0. No start, stop or erase command sent. |
| GPS on the pre-existing firmware | VALID checksum NMEA at 38400, current RAM readback verified 100 ms / RMC 10 Hz / GGA 1 Hz, checksum_fail=0; NO FIX at observation. This is not a test of the corrected candidate or GPS standby/wake. |
| Battery status | VBUS=1, cell=0, calibrated=0, trusted=0; unvalidated nominal ADC around 4.113 V. No voltage accuracy or battery-presence inference. No battery mode/calibration changed. |
| External SPI flash | FAIL: all five JEDEC reads returned `000000`; status, REMS and both SFDP passes were all zero, qualification UNRESOLVED. Repeatability reported PASS for the identical zero responses, which is not valid identification. |
| Preserve/read sessions | BLOCKED: the read-only `TCB READ` request returned `TCB REFUSED: JEDEC...`, not TPX3. The captured 20-byte prefix is preserved; it is not a valid session backup. Existing session/data integrity remains INCONCLUSIVE. |
| Upload / GPS cycles | NOT RUN: no reset, loader entry, firmware upload or `GPS BACKUP TEST` command. Cycles 1–3 and post-cycle recording readiness remain INCONCLUSIVE. |

The serial port was closed after the failed read-only preflight. No recording/erase command, automatic standby activation, firmware edit, recovery procedure, second upload, merge or push followed the failure. No conclusion about whether recordings are present, absent, damaged or erased can be drawn from the unavailable flash readback.

Evidence: `logs/hardware-gps-backup-2026-11-03/hardware-run-20261103/` contains `candidate-verification.json`, `preflight-result.json`, `before-upload-serial.txt` (full status/identity transcript), `before-upload.tpx3` (the invalid export/refusal prefix, not a backup), and `hardware-result.json`. The host preflight script was tightened to require the expected nonzero flash identity and resolved qualification; only this evidence/host tooling and the report were updated after the failure, not firmware.

Next required owner intervention: inspect the external flash module's power and SPI connections, then confirm when the device is ready for another non-destructive preflight. Do not infer a GPS backup failure: the GPS backup test was never run. The previously approved candidate and its checksums remain unchanged.


## Read-only preflight retry after owner reported ready

**BLOCKED — same external flash communication failure persisted.** Previous evidence was preserved; this retry is recorded separately in `logs/hardware-gps-backup-2026-11-03/hardware-run-20261103-retry1/`.

- COM8 again matched VID:PID 2886:8044 and serial 0E8A89B7289430BD. The corrected source, ELF/package identity and all selected artifact checksums still match the approved manifest. Firmware was not rebuilt or modified.
- Recorder idle checks passed again: V1 STOPPED, OBS/fast IMU OFF, COMP idle/no fault/empty queues, TCB state=1/error=none/lock=0.
- Existing firmware GPS: valid checksum NMEA at 38400, no checksum failures, CURRENT RAM readback verified 100 ms / RMC 10 Hz / GGA 1 Hz; NO FIX. These observations still do not test the corrected candidate or GPS standby/wake.
- All five JEDEC reads were `000000`. Status/REMS/SFDP remained all zero and qualification UNRESOLVED. Identical responses reporting repeatability PASS do not establish a working flash device.
- The strengthened preflight stopped at identification. No session-export retry, reset, bootloader entry, firmware upload, GPS backup command, recording/erase command or recovery procedure followed. The serial port was closed. Recording contents and all three GPS cycle outcomes remain INCONCLUSIVE.
- Full raw serial evidence: `before-upload-serial.txt`; structured checks: `preflight-result.json`, `candidate-verification.json`, `hardware-result.json` in the retry directory.

The observed result narrows the blocker to the external SPI flash communication path, not the candidate checksum or GPS UART. Software observations cannot distinguish missing power, wiring/contact problems, a held/disabled flash or a device fault. The expected SPI mapping remains CS D2, SCK D8, XIAO MISO D9 to flash DO, XIAO MOSI D10 to flash DI, with shared ground and the documented module supply. Physical connection/supply verification is required before another upload preflight; no firmware workaround was attempted.

## Read-only source and flash-state investigation

At the owner's instruction, uploads and GPS backup testing remain explicitly blocked. See [flash-detection-investigation.md](flash-detection-investigation.md) for the completed comparison of boot/wake release, pin mapping, CS/SPI framing, standby policy and diagnostic readiness with the previously working `155a999` firmware.

The flash paths and pin mapping match that working source. Boot and standby exit send `0xAB` and wait 100 us, but identity diagnostics do not independently wake the flash or verify release completion. An ineffective/missed release is the leading software hypothesis, not a proven cause; a runtime SPI/electrical fault remains possible. The legacy `DIAG` reader also returned zero IDs and an invalid SFDP signature during this investigation, supporting a failure below the raw diagnostic formatter. No wake/reset/status/program/erase operation, firmware edit, upload or GPS backup command was performed. The serial port was closed; candidate hashes remain unchanged.

**Correction to earlier interpretation:** `qualification=UNRESOLVED` is hard-coded in every successful raw identity report and also appears in historical known-good captures. It is not an identification failure. The host preflight's added rejection of this field must be removed before any future preflight use; validate the actual JEDEC and SFDP instead. The observed five zero IDs and all-zero SFDP still independently establish failed identification. The host script was not changed in this read-only investigation.

The smallest proposed non-destructive follow-up is one explicitly authorized idle-only wake (`0xAB`, CS HIGH for the existing 100 us), followed by JEDEC/SFDP readback; it changes power state and was not attempted under read-only authorization. Do not re-enable uploads or start GPS cycles merely because this proposal has been documented. Physical root cause remains INCONCLUSIVE.

## Authorized wake diagnostic: blocked by existing firmware capability

The owner subsequently authorized one controlled wake-then-read diagnostic only through an existing safe firmware command, and authorized correcting the host preflight. COM8 identity and inactive recording/export/capture states were rechecked and passed. The reconnect menu and known serial handlers expose no flash-wake/raw-SPI command; `AB` is internal to boot/standby exit and excluded from identity diagnostics. In accordance with that limitation, the test stopped **without sending AB, resetting, uploading or altering firmware**. There are no after-wake JEDEC/SFDP responses; communication restoration remains NOT TESTED. No erase, array/status write, recording start, merge or push occurred. Serial was released.

The host preflight's erroneous qualification rejection is now corrected. It ignores `qualification=UNRESOLVED` for admission and checks actual repeated `0B4018`, the verified idle/protection status, complete SFDP passes and exact saved-reference bytes. **11/11 offline validation cases passed**, including acceptance of the genuine historical unresolved-label capture and rejection of the real all-zero capture. Firmware/test/build hashes are unchanged; uploads and GPS backup cycles remain explicitly blocked.

Evidence is in `logs/hardware-gps-backup-2026-11-03/flash-wake-capability-check/`; details and the corrective-action limits are in [flash-detection-investigation.md](flash-detection-investigation.md). Physical flash supply-voltage and wiring/continuity/HOLD/reset checks are recommended before further firmware changes. Existing recording integrity remains unverified, not assumed erased.

## Latest requested DIAG ID recheck — PASS

At the owner's request, COM8 was opened at 115200 using a direct serial connection because this assistant cannot control VS Code's monitor UI. The first request was refused while the GPS profile operation was active. That complete transcript (`diag-id-recheck-20261103-090933/serial-transcript.txt`) subsequently showed the firmware's automatic legacy report with five valid `0B4018` IDs and valid SFDP. The initial host marker timeout reflects that refused raw command, not failed identification in the later automatic report.

After that operation finished, idle status checks passed and a second `DIAG ID` request completed successfully:

- JEDEC reads 1–5: **`0B4018`, `0B4018`, `0B4018`, `0B4018`, `0B4018`**.
- SR1 before/after: `00` / `00`; SR2: `02`.
- SFDP header: **`53 46 44 50 00 01 01 FF`**.
- Both complete 256-byte SFDP passes matched the previously verified device reference byte-for-byte; reference SHA-256 `fb55c78de1f723eb5048b7f004658c75128c515881efae7d9dcc918afafafdec`.
- Corrected host communication validation: **PASS**, independent of the informational `qualification=UNRESOLVED` text.

Full raw output: `logs/hardware-gps-backup-2026-11-03/diag-id-recheck-20261103-091015/serial-transcript.txt`; structured response: `result.json` beside it. Serial was closed afterward. No host wake/reset, upload, flash write/erase or recording command was issued. The presence of new profile/automatic-report output does not establish why the communication recovered; no claim is made that a particular wake, power or wiring action cured it.

This restores identification/read-only SFDP communication only. Recording contents were not downloaded or revalidated. The earlier failure and evidence remain preserved; firmware upload and GPS backup cycles remain blocked until the owner authorizes continuation.


## Final read-only preflight after complete power cycle — READY FOR FLASH APPROVAL

The owner reports recovering flash communication by removing **both USB and battery power**, then reconnecting. This history supports investigating retained device/power state, but does not prove deep power-down was the specific cause. No reset or power action was performed by the assistant.

The owner requested a final read-only preflight and expressly required stopping for **further explicit flash approval** afterward. The following checks passed against the existing installed firmware, not a newly uploaded test image:

| Gate | Final result |
| --- | --- |
| Device | COM8, 115200 baud, VID:PID `2886:8044`, serial `0E8A89B7289430BD`. |
| Idle before and after download | V1 STOPPED, OBS/fast IMU OFF, COMP idle/no fault/empty queues, TCB state=1/session=0/error=none/lock=0/programs=0. Recording append pointer remained `0x0A1000`. The complete export ended and status commands responded normally afterward; no export remains active. |
| Flash communication | Five identical `0B4018` reads. SR1 before/after `00`, SR2 `02`. Both complete 256-byte SFDP passes matched the saved reference exactly; header `53 46 44 50 00 01 01 FF`. Informational qualification text did not determine admission. |
| Read-only recording export | Complete TPX3 download, 721,344 bytes, 2,578 packets; packet sequence/end framing and CRC checks passed. Export SHA-256 `9cdc66e807012d2330439fde8761e3137cc7a3431627269c856fd2974c39bb72`. Reconstructed image 659,456 bytes, SHA-256 `d1c398b03deee2e8ea88a86779e856154ab0dfbac4a1165df1f62faf678e0379`. |
| Older recordings preserved | Entire 647,168-byte recording prefix from the saved USB-control baseline is byte-identical, SHA-256 `e8e62ef464f9f5e4a1f116a7d8eaa7806c3bc7152443e1cb5bafc2f10c0f2848`. All 13 baseline sessions remain preserved. |
| Available integrity checks | Transport/page/block/footer decoding found **no new issues**; legacy decoder issues empty. The known historical interrupted final block in session 133 at `0x087000` is unchanged. This is not a claim that every historical session is complete. |
| Additional pre-existing data | Session 159 is complete, with 1,000 IMU / 100 RMC / 10 GGA records, offered=committed, lost/rejected/missed/CRC/incomplete/full counters zero. It is additional to the older baseline, not created in this run: it lies within the append range already recovered before the preflight, and before/after TCB programs=0/session=0 and append pointer are unchanged. Its origin is not inferred. Therefore the *entire* current image is not byte-identical to the smaller older image; its entire older prefix is. |
| Existing-firmware GPS context | Valid checksum NMEA at 38400, CURRENT RAM readback verified 100 ms / RMC 10 Hz / GGA 1 Hz; valid fix at observation. No GPS backup command or new cadence test was run. |
| Corrected candidate | All candidate source/test hashes, unchanged remaining firmware/variant/config bytes, local patch identity and selected build artifacts still match the tested manifest. The candidate was not rebuilt or uploaded. Automatic GPS backup remains off in the selected environment. |

Candidate environment: `tacticpod_reusable_usb_gpsbackup`.

- Base commit: `29f90966d868f68b85d63d64d27bab9584f7628a`, plus the same uncommitted recovery-gate correction.
- Patch SHA-256: `0a4ffaa21e61a320e9e30ef2f3b79271afc2f757808063fd0c1312c876aa8eaa`.
- Application BIN SHA-256: **`aced1874981173ebfbb3904390f6088214aca3419f72f6a5fae0deb4f4c14051`**.
- Application-only DFU ZIP SHA-256: **`91727831db63198d2c007134803526125a096adc47eeecbe1d8c3879e2004e28`**.
- ELF-to-BIN conversion matches the ZIP payload; all manifest artifact/source hashes were checked again after the preflight.

Evidence directory: `logs/hardware-gps-backup-2026-11-03/final-readonly-preflight-20261103-091526/`. It contains the full serial transcript, candidate verification, complete TPX3 archive, reconstructed image, decoded records and `result.json`. The status transcript records all commands; none start/stop a recording, wake/reset, erase, program, alter status/configuration, upload, merge or push. Serial was closed.

**Result: READY FOR FLASH APPROVAL.** This means the final read-only preflight passed, with the explicit historical session-133 caveat. It is not approval to flash and does not qualify GPS backup operation or pilot/production readiness. The assistant stopped here. Firmware upload, GPS backup cycles, merge and push require further explicit owner approval; previous upload permission is not reused for this next step under the owner's latest instruction.


## Explicitly approved upload and supervised GPS backup test — FAIL

The owner explicitly approved uploading the exact corrected supervised-test candidate, BIN SHA-256 `aced1874981173ebfbb3904390f6088214aca3419f72f6a5fae0deb4f4c14051`, and up to three cycles only while each preceding cycle passed and fully recovered. No automatic GPS standby, recording/erase, merge, push or release was authorized.

### Before upload — PASS

Source/patch and artifact hashes were verified again. The package payload, saved BIN and independent ELF-to-BIN conversion all match the approved SHA-256. ZIP SHA-256 remains `91727831db63198d2c007134803526125a096adc47eeecbe1d8c3879e2004e28`. The ZIP contains only an application component, with existing SoftDevice requirement `0x0123`; there is no bootloader/SoftDevice image or intentional external SPI flash erase operation.

COM8 again matched `2886:8044`, serial `0E8A89B7289430BD`. Idle, fresh NMEA/verified GPS profile and flash-ID/SFDP checks passed. A fresh read-only recording download was byte-identical to the complete final-preflight image (including all stored sessions and the known historical interrupted session 133); idle was rechecked after export. No record/erase command was sent.

### Upload and startup — PASS

One 1200-baud loader entry moved the same device to COM7, VID:PID `2886:0045`, with the same unique serial. The initial helper stopped before invoking DFU because this serial-DFU entry exposed no UF2 disk/INFO_UF2 file. Read-only volume/serial inspection confirmed no mounted loader disk. No firmware bytes had been sent by that helper.

The already-entered serial loader was then used without another reset, matched against the confirmed application's unique serial and expected bootloader USB identity. The historical Sense identity remained the board provenance; no fresh INFO_UF2 read is claimed. Exactly **one** Adafruit `nrfutil dfu serial ... -b 115200 --singlebank` process sent the approved application ZIP. It exited 0 and reported `Activating new firmware` / `Device programmed.` The application returned on COM8.

At 115200 baud, the startup menu included `GPS BACKUP TEST`. Checks before cycle 1 confirmed:

- V1 STOPPED; OBS/fast IMU OFF; COMP idle, no fault, empty queues; TCB state=1/session=0/error=none/lock=0/programs=0.
- Five `0B4018` IDs, SR1 before/after `00`, SR2 `02`; SFDP header `53464450000101FF`; both complete 256-byte passes matched the verified reference.
- Checksum-valid NMEA at 38400, cumulative checksum failures 0, fresh message age 27 ms at observation; CURRENT RAM readback verified 100 ms / RMC 10 Hz / GGA 1 Hz. This is configuration verification, not a separately measured navigation-solution cadence.
- The selected environment enables the supervised command only. Automatic GPS backup remains disabled.

### Cycle results

| Check | Cycle 1 | Cycles 2–3 |
| --- | --- | --- |
| Standby request written to UART | YES | Not run |
| Silence after 3000 ms parked / within 1500 ms listen check | **NO — incoming UART traffic observed** | Not run |
| Explicit `0xFF` wake attempted | **NO** | Not run |
| Time to valid NMEA from first wake | Not measured; Wake phase never entered | Not run |
| Post-wake baud / 10 Hz / fix | Not checked; Wake/Profile/Fix phases not reached | Not run |
| Post-cycle flash and recording-readiness checks | Not run after failure; immediate stop | Not run |
| Overall | **FAIL — standby not confirmed** | Not run because cycle 1 failed |

Exact cycle output:

```text
GPS BACKUP TEST started (about 20 s, USB stays awake; recording blocked). Wake mask 0x8
GPS BACKUP TEST RESULT (wake mask 0x8)
  standby request sent: YES
  standby confirmed (UART silent 1500 ms after 3000 ms parked): NO (NMEA kept flowing; receiver did not enter standby)
  UART wake attempted: NO
  time until valid NMEA: NOT RETURNED
  GPS baud 38400: NOT CONFIRMED
  10 Hz configuration: NOT CHECKED
  GPS fix: NOT CHECKED
GPS BACKUP TEST: FAIL - standby not confirmed; receiver kept running (baseline power). Wake was not tested.
GPS BACKUP TEST END
```

Interpretation limits: `standby request sent` confirms the MCU UART accepted the request, not an acknowledged receiver acceptance. The state machine fails on incoming bytes during its listen phase. Its text says NMEA kept flowing, but the failure trigger itself is a byte-count change; a separate post-cycle checksum-valid NMEA/cadence check was not performed. This result cannot distinguish failure to enter standby from an early/unintended wake when UART was restarted. `NOT RETURNED` in this branch is not evidence of a failed attempted wake: **there was no explicit wake attempt**. No root cause or alternative wake mask is established by this test.

### Immediate stop and data safety

After the failed result, no more host commands were sent. No second/third cycle, baud scan, receiver reset, recovery command, flash diagnostic or additional upload was attempted. Serial was closed. The approved test firmware remains installed; its existing asynchronous UART/profile servicing was not disabled or modified. Firmware source/test/build hashes are unchanged. Automatic GPS backup stays off. No host recording, stop, erase, flash status/configuration write, merge, push or release was performed.

The pre-upload recording backup is preserved. A post-upload/post-cycle byte-for-byte export was not performed because the owner required immediate stop on failure. Do not claim physical post-upload session integrity or recording readiness was verified. Source handling of EntryNotConfirmed does not set the failed-wake latch, but the resumed UART's profile audit may still be pending; no recording start was used to probe admission.

Evidence: `logs/hardware-gps-backup-2026-11-03/hardware-run-approved-20261103/` contains `pre-upload-serial.txt`, the complete pre-upload TPX3/image/decoded backup, `candidate-verification.json`, `preflight-result.json`, loader/DFU intent records, `upload.txt`, `upload-result.json`, **`post-upload-and-cycles-serial.txt`** (full raw startup/checks/cycle transcript), `cycle-1-response.txt`, `cycle-1.json`, `test-result.json` and `hardware-summary.json`.

**Outcome: upload/startup PASS; physical supervised GPS backup test FAIL; wake recovery and subsequent cycles INCONCLUSIVE/not tested.** Experimental automatic GPS standby and pilot release are not qualified. Results are reported now, before any further firmware modifications or uploads, as requested.

## Offline cycle-1 investigation

See [gps-backup-cycle1-investigation.md](gps-backup-cycle1-investigation.md) for the seven-point source/transcript review and primary u-blox references. The exact approved source and artifact hashes still match. No device connection, firmware/test change, reset, upload or rerun occurred during that investigation.

The leading cause is a command-flag mismatch: the approved PMREQ omits the extra bit required by the MAX-M10S software-standby instructions. The proposed correction is documented, not applied. The UART RX wake selection is supported by the target's interface description.

The saved result proves a 24-byte MCU write was accepted and a non-UBX input counter changed after UART restart. It does not prove full on-wire transmission/receiver acceptance, exact byte arrival timing, complete valid NMEA, or a failed attempted wake. PMREQ is treated as unacknowledged by u-blox's reference implementation. The BSP clears the software RX ring on `Serial1.end()`, but restart-related hardware events/line transitions are unmeasured. No raw GPS UART trace or phase timestamps were saved, so those details cannot be reconstructed honestly. The report's literal `NMEA kept flowing` remains historical test output, not a separately validated finding.

Evidence and reconstructed packet/checksum are saved under `logs/hardware-gps-backup-2026-11-03/gps-backup-cycle1-investigation/`. Backup-mode breakout wiring and reliable post-standby configuration/baud recovery remain unverified. No further testing or corrective firmware deployment is authorized by this report.


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


## Approved field firmware restoration: preflight stopped (2026-11-03)

Target: commit `155a999122f0ce85ab6817c6eba690e9501fe4b3`, environment `tacticpod_reusable_usb`, archived application 202320 bytes, BIN SHA-256 `40bced328dbcc453eb8326297fabab40c268063140d6c909a4359b4024a9a585`, application-only DFU ZIP SHA-256 `c38561e5004ec88bf5d1b6b8b21c4d50a55cbbd19db41a7065c804438a6ae007`. Both hashes and embedded application bytes match. Source at the approved commit lacks experimental GPS backup functionality. Package contains application only and existing S140 requirement 0x0123; the planned upload does not intentionally erase external SPI flash.

Before device access, LED behavior was confirmed directly from that commit: application boot/idle/standby OFF; start acceptance two short blue flashes; actual recording start 400 ms blue; recording pulses every 3 seconds blue without fresh fix or alternating blue/green with fresh fix; trusted low battery orange overrides those pulses; clean committed stop two short green flashes; start failure a red triple-flash burst; persistent fault repeating red triple-flash every 2 seconds. There is no physical power-off button or dedicated application power-on indication. Bootloader/charging LEDs are separate. No LED/recording test was run.

An initial host-only metadata typo was corrected before any device identification or serial access; its local aborted record is retained separately under `field-rollback-approved-20261103/`. The subsequent single device preflight confirmed COM8 VID:PID 2886:8044, serial `0E8A89B7289430BD`, matching the XIAO. Recorder STOPPED; OBS/fast IMU OFF; COMP capture off and queues empty; TCB idle, unlocked, no error. No active export observed; all status commands replied normally.

GPS readiness then failed:

```text
GPS STATUS | UART=Serial1 RX:D7 TX:D6 38400 | NMEA=NONE | bytes=11797 | checksum_ok=161 | checksum_fail=0 | last_valid_message_age_ms=N/A
GPS configuration: UNVERIFIED (no receiver communication after wake/start)
GPS ERROR: UART data stopped; check power and TX wiring.
```

Historical valid-checksum/profile information is not proof of current readiness. Serial was closed immediately, as the owner required for unresponsive GPS. No DIAG ID, new recording export/backup, bootloader entry, reset, upload or experimental recovery followed. Existing backups remain preserved; no fresh complete backup or current flash health claim is made from this attempt.

Outcome: **rollback not performed; not ready for a separately approved USB recording test**. No firmware modifications, recording tests, erases, pushes, merges, tags or releases. The approved archived BIN/ZIP remain unchanged. No further action was taken.

Evidence: `logs/hardware-gps-backup-2026-11-03/field-rollback-approved-ready-20261103/`: `candidate-verification.json`, `pre-upload-serial.txt`, `latest-gps-status.json`, `preflight-result.json`, `hardware-summary.json`, `evidence-manifest.json`. Host script `approved_field_rollback.py` was used only for preflight; no upload procedure was invoked.
