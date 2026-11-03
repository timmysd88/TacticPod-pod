from pathlib import Path

root = Path(__file__).resolve().parents[3]
report = root / 'docs/hardware-gps-backup-test-report.md'
report.write_text('''# GPS backup hardware test report

Date: 2026-11-03 (Europe/London). **Local recovery-gate correction; hardware tests await explicit upload approval.**

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

**Complete regression run: PENDING final collection.** The full 32-suite runner was started on the corrected source. Results and logs will be recorded here when it finishes. The additional linked startup test already passed against the rebuilt reusable/default ELFs. These are host simulations/ARM emulation, never physical hardware verification.

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

## Connected hardware and flashing — BLOCKED pending approval

Enumerated application: **COM8**, USB VID:PID **2886:8044**, serial **0E8A89B7289430BD**. This identifies the USB device; it does not prove installed firmware, idle recorder state, baud, sensor function or session integrity. USB monitor should use **115200 baud**; GPS UART remains **38400 baud** in source.

Expected loader: existing XIAO Adafruit-compatible UF2/serial DFU. The historical 2026-11-02 `INFO_UF2.TXT` capture records UF2 0.6.1, Seeed_XIAO_nRF52840_Sense, S140 7.3.0. It has not been re-entered/verified in this run. Prior evidence used loader PID `0045`; re-identify the same physical device and actual loader port rather than assuming COM8 or a PID from the board manifest.

After explicit approval for this exact candidate, close the monitor, verify source/patch/artifact hashes, confirm idle/no export with existing non-destructive diagnostics and preserve sessions, then use 1200-baud touch or the existing double-tap RESET entry. Verify loader identity and upload the application-only ZIP with bundled Adafruit `nrfutil dfu serial -pkg <firmware.zip> -p <verified-loader-port> -b 115200 --singlebank`. Re-enumerate the application and monitor at 115200. The repository records a historical direct-PlatformIO HEX/ZIP mismatch; use the inspected ZIP uploader path. No bootloader/SoftDevice image or mass erase is involved.

If USB fails to recover, use documented double-tap RESET and inspect the loader; stop before unknown recovery actions. If GPS fails to wake, stop all further cycles. Firmware documents removing both USB and battery as the required power-cycle; that is owner intervention, not an action performed or newly verified here. Do not improvise wake masks, baud changes, resets or recovery commands.

No serial port was opened, no command sent and no device reset/upload performed during this correction.

## Physical GPS cycles — INCONCLUSIVE

| Cycle | Request accepted | Standby silent | Wake / valid NMEA | Time to NMEA | 38400 / 10 Hz | Fix |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | Not run | Not run | Not run | Unmeasured | Unverified | Unverified |
| 2 | Not run | Not run | Not run | Unmeasured | Unverified | Unverified |
| 3 | Not run | Not run | Not run | Unmeasured | Unverified | Unverified |

No hardware command transcript exists yet. After approval, run `GPS BACKUP TEST` only while idle/no export and fresh NMEA/10 Hz verification are established. Capture the complete serial output. Repeat only after PASS and full recovery, up to three cycles total; stop immediately on failure, incomplete recovery or unexpected behaviour.

## Recording, LED/button, battery and standby limitations

| Check | Source/host evidence | Physical status |
| --- | --- | --- |
| Recorder admission and session safety | Entry guards and failure latch preserved; new integration checks both recording admission APIs remain blocked during the test and after failed recovery; no simulated flash program/erase/write-enable permitted by these cases. | INCONCLUSIVE: real idle/export state, sessions, partition/free capacity and post-wake admission not queried. No real recording started or flash data modified. |
| IMU | Source sets sensor ODR 104 Hz, with acquisition scheduled at 100 Hz. | INCONCLUSIVE: actual registers, timing and missed samples unmeasured. |
| GPS | Source UART 38400; actual parser and independent profile model exercised by tests; real 10 Hz readback is mandatory before PASS. | INCONCLUSIVE: physical NMEA, rate, RAM profile, standby/wake and fix unmeasured. |
| Flash | Existing read-only identification/status diagnostics and bounded session partition remain unchanged. | INCONCLUSIVE: physical identification, partition and existing sessions not read. |
| Button | 25 ms debounce; 3 s start hold; stop is a tap at most 500 ms, second press within 5 s held 3 s; held-at-boot start remains blocked. | INCONCLUSIVE: no physical button/recording test. |
| RGB LED | Recording green 100 ms every 2 s; start green 1 s; clean stop two green flashes; event/fault/low-battery priority remains unchanged. | INCONCLUSIVE: no visual or timing measurement. |
| Battery | Explicit cell mode, RAM calibration, fresh valid samples and warm-up required for trust. Poll 1 s; thresholds 3600/3500/3300/3700 mV. USB mode does not infer cell presence. | INCONCLUSIVE: wiring/presence, ADC accuracy, calibration and low-voltage behaviour unverified. No mode/calibration changed. |
| Standby | System ON; VBUS/serial and active/faulted operations block entry. Settle 2 s; button polls 100/10 ms; peripherals wake before a valid start; LEDs off in standby. | INCONCLUSIVE: battery-only entry/wake and current draw unmeasured. Automatic GPS standby remains off. |

## Readiness and outcomes

**Supervised hardware test:** software correction and focused integration/negative-control checks pass; final readiness awaits completion of the full regression collection below. Actual upload still requires explicit owner approval for the selected environment and exact candidate hashes. No approval to flash is inferred from permission to correct the source.

**Pilot release:** BLOCKED. `tacticpod_reusable_usb` is the normal reusable environment, but mandatory physical checks are incomplete. No `v0.1.0-pilot` release, tag, publication or deployment was created. No production readiness is claimed.

- **PASS** — Synchronisation, focused recovery regression tests, negative control, four builds and linked startup verification.
- **FAIL (historical, corrected locally)** — The unchanged PR head's circular recovery observation; retained in the baseline evidence.
- **INCONCLUSIVE** — All physical GPS, recorder, flash, IMU, LED/button, battery and standby checks.
- **BLOCKED** — Device upload requires explicit approval; pilot preparation requires the outstanding hardware gates.
''', encoding='utf-8')
print('Updated report with correction and hardware limitations')
