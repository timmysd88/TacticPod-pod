# GPS backup hardware test report

Date: 2026-11-03 (Europe/London). Status: **BLOCKED** before device upload.

## Source synchronisation — PASS

- Repository: `timmysd88/TacticPod-pod`; origin verified as `https://github.com/timmysd88/TacticPod-pod.git`.
- Original branch: `main`, commit `155a999122f0ce85ab6817c6eba690e9501fe4b3`.
- Original working tree: dirty; one tracked modification (`logs/stock-battery-review/compiled-startup-results.json`) and 3,236 untracked files across eight log directories. All preserved.
- `git fetch origin --prune` succeeded. `git fetch origin pull/6/head` succeeded.
- [PR #6](https://github.com/timmysd88/TacticPod-pod/pull/6): **open, not merged**, verified through connected GitHub metadata.
- GitHub head and freshly fetched head both equal `29f90966d868f68b85d63d64d27bab9584f7628a`.
- Selected source/build commit: **`29f90966d868f68b85d63d64d27bab9584f7628a`**. This is not a claim that the commit has been tested on hardware.
- Active testing branch: `test/pr6-gpsbackup-short-20261103`, worktree `.p6` under the original repository. Clean at creation; current Git status is not clean after reports, generated test outputs and LF checkout normalization (Git reports checkout/stat differences). A separate byte comparison against the committed archive verifies all 45 firmware/variant files match exactly; there are no firmware source edits. See `source-integrity.json`.
- The earlier worktree `.worktrees/pr6-gpsbackup-20261103`, branch `test/pr6-gpsbackup-20261103`, remains preserved. Its deeply nested compiler path exceeded Windows path limits. Git refused to move it because it contains library submodules; the shorter worktree uses the same exact commit.
- No commit, stash, reset, clean, merge, tag, push or publication was performed.

## Firmware preparation — build PASS; source safety check FAIL

PlatformIO Core 6.2.0, pinned Seeed platform `a6fd41debbbcda13b5340fff50c9092ed3289194`, ARM GCC 7.2.1, framework-arduinoadafruitnrf52 1.10101.0. Libraries match the CI pins: LSM6DS3 `e063c3a544b8bb89238accddf24f0028bcd1ca20`, TinyGPSPlus `ca29434514a5c5172bd807af0608df7f296582a2`. Installed packages are reused through a worktree junction; firmware build outputs are separate.

| Environment | Result | Static RAM | Flash |
| --- | --- | ---: | ---: |
| `seeed_xiao_nrf52840_sense` | PASS | 118,992 bytes | 166,360 bytes |
| `tacticpod_reusable_usb` | PASS | 122,040 bytes | 202,192 bytes |
| `tacticpod_reusable_usb_gpsbackup` | PASS | 122,080 bytes | 207,520 bytes |

Build command: `python -m platformio run -e seeed_xiao_nrf52840_sense -e tacticpod_reusable_usb -e tacticpod_reusable_usb_gpsbackup`.

The selected upload environment would be `tacticpod_reusable_usb_gpsbackup`. It inherits the Sense, flash and reusable flags, and adds only `-DTACTICPOD_GPS_BACKUP_TEST=1`. `TACTICPOD_GPS_BACKUP_STANDBY` remains zero. The `_auto` environment was not selected for upload.

### Blocking defect in the unmodified PR

In `src/gps_receiver.cpp`, `backupStep()` sets `o.profileVerified = recordingReady()`. In the test build, `recordingReady()` requires `!backupTestRunning()`. The test remains running throughout `GpsPower::BackupTest::Phase::Profile`, so `o.profileVerified` is always false there. Consequently, even a receiver that resumes valid NMEA and verifies its RAM profile cannot reach the successful Fix phase; it must fail or time out after 45 seconds.

This is **source-code verification**, not a measured GPS wake failure. The portable state-machine unit test supplies its own observations and does not exercise this receiver integration. See `logs/hardware-gps-backup-2026-11-03/check_profile_gate.py` and `profile-gate-finding.json` for the read-only source proof. The firmware has not been patched, so its provenance remains the exact PR commit. A correction must distinguish raw GPS profile recovery from permission to record while keeping the recording guard in place.

## Regression tests

The repository's full 32-suite host regression runner completed: **30 passed directly, 2 failed in the raw run (exit code 1)**. Both failed suites subsequently passed with the explicitly documented setup adjustments below. These tests use host models/ARM emulation and do not access the physical device. Do not interpret this as a clean, unmodified runner pass.

| Check | Final evidence |
| --- | --- |
| 30 other suites | PASS: GPS, recording, storage/flash identity, compression/decoder/timing/RAM, append/movement, reusable integration, battery/ADC, console/download, button/LED, startup, standby and field scenarios. |
| `tests/recorder_v2/run_tests.py` | Initial byte-exact fixture failures from CRLF checkout. PASS on rerun with exact committed LF bytes: all 32 test cases. Log: `recorder-v2-lf-rerun.txt`. |
| `tests/reusable/gps_backup_tests.py` | Raw ARM fallback could not link newlib stdio (`_sbrk`, `_write`, etc.); host `g++` is unavailable. PASS on rerun of the unchanged suite using test-only `--specs=nosys.specs` stubs via `run_gps_backup_arm.py`. Log: `gps-backup-nosys.txt`. Neither firmware nor repository test sources were changed. |
| Additional linked startup check | PASS: reusable/default variants and inherited GPIO condition model, using newly built ELFs. Log: `compiled-startup.txt`. |
| Receiver profile-gate source check | FAIL: circular gate described above. Portable unit tests do not cover this live receiver integration. |

All 32 suites therefore have passing host evidence, with two qualified reruns; the independent source blocker still prevents hardware progression. The raw results are preserved separately from the qualified summary in `logs/hardware-gps-backup-2026-11-03/regression-summary.json` and `regression-logs/`.

Initial long-path results are preserved in `logs/initial-long-path-regressions/`; those compiler failures do not establish firmware defects. The short checkout initially inherited `core.autocrlf=true`, causing byte-exact JSON fixture mismatches. Committed files in the isolated short worktree were restored to exact committed LF bytes only where the sole difference was CRLF conversion; no original-worktree file or Git configuration was changed. See `checkout-normalization.json`. Fixture failures must be rerun after this correction.

## Connected hardware and upload method

- Port enumeration: **COM8**, USB VID:PID **2886:8044**, serial **0E8A89B7289430BD**. Matches the XIAO application identity and prior captures. Enumeration alone does not verify installed firmware, sensors or recorder state.
- Expected monitor: 115200 baud. GPS UART: 38400 baud, as configured in `src/gps_uart_config.h`; current physical baud and NMEA are **INCONCLUSIVE**.
- Expected bootloader: Adafruit-compatible XIAO UF2/serial DFU. Historical `logs/field-trial-2026-11-02/INFO_UF2.TXT` records UF2 0.6.1, Seeed_XIAO_nRF52840_Sense, S140 7.3.0. The bootloader was not entered or freshly inspected during this run.
- Expected upload: 1200-baud touch (only after approval), re-enumerate the same device in bootloader mode, then bundled Adafruit `nrfutil dfu serial -pkg <firmware.zip> -p <verified-loader-port> -b 115200 --singlebank`. Prior local upload evidence used loader PID `0045`; do not assume the application port or board manifest's PID list identifies the active loader.
- The DFU ZIP was inspected offline: application-only payload, SoftDevice requirement `0x0123`, no bootloader/SoftDevice update. The application BIN was extracted without modification; SHA-256 values and exact flags are in `preflight-manifest.json` and `SHA256SUMS.txt`.
- Repository upload notes record that direct PlatformIO upload previously supplied HEX to a ZIP uploader. Use the inspected ZIP and bundled Adafruit uploader, after independently confirming the loader identity.
- No serial monitor was opened, no command sent, and no reset/upload performed.

## GPS cycles — INCONCLUSIVE

| Cycle | Request accepted | Standby silent | Wake / valid NMEA | Time to NMEA | Baud / 10 Hz | Fix |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | Not run | Not run | Not run | Unmeasured | Unverified | Unverified |
| 2 | Not run | Not run | Not run | Unmeasured | Unverified | Unverified |
| 3 | Not run | Not run | Not run | Unmeasured | Unverified | Unverified |

There is no hardware command transcript because no cycle was attempted. Stop on the first failure or incomplete recovery; subsequent cycles require a passed cycle and full recovery. No alternative wake mask, baud scan, reset or recovery procedure was attempted.

## Recorder, button, LED, battery and standby checks

| Check | Source-code finding | Physical status |
| --- | --- | --- |
| Recorder safety | Backup command checks recording, session/export, capture, recorder faults, UART busy, GPS busy, fresh NMEA and verified profile; recording is blocked during the test. Failed wake/profile can latch a recording block. | INCONCLUSIVE: idle state, no export, session preservation and post-wake admission not read from device. |
| IMU | Sensor ODR is 104 Hz; recorder schedules acquisition at 100 Hz. A 100 Hz print/sample claim must not be confused with the sensor ODR. | INCONCLUSIVE: register configuration, rates and missed deadlines unmeasured. |
| GPS | UART default 38400; recording admission requires the existing verified 10 Hz RAM profile. | INCONCLUSIVE: no fresh NMEA, rate or profile readback captured. |
| Flash | Read-only identification and status diagnostics exist; reusable sessions use a bounded partition and preserve old sessions. | INCONCLUSIVE: identification, partition, free sectors and existing sessions not queried. |
| Button | 25 ms debounce; start after 3 s hold; stop uses a tap of at most 500 ms, then another press within 5 s held for 3 s. Held-at-boot start is blocked. | INCONCLUSIVE: no button presses or recording started. |
| RGB LED | Recording green pulse is 100 ms every 2 s; start is solid green 1 s; clean stop has two green flashes. Higher-priority event, fault and low-battery patterns may replace the pulse. | INCONCLUSIVE: no visual/timing measurement. |
| Battery | Trust requires explicit cell mode, RAM calibration, valid fresh samples and warm-up. Poll is 1 s; default warning/stop/hard/recover thresholds are 3600/3500/3300/3700 mV. USB mode does not infer cell presence. | INCONCLUSIVE: cell wiring/presence, ADC accuracy, calibration and low-voltage cutoff behaviour unverified. No battery mode/calibration changed. |
| Standby | System ON only; VBUS/serial, recording, busy sessions, faults and operations prevent entry. Settle 2 s, released-button poll 100 ms, pressed poll 10 ms; a valid start wakes peripherals first. LEDs off in standby. Automatic GPS backup remains disabled. | INCONCLUSIVE: entry/wake, current draw and battery-only operation unmeasured. |

No recordings were started/stopped or erased; no flash-data mutations or destructive diagnostics were requested.

## Pilot firmware readiness — BLOCKED

Normal reusable environment identified: `tacticpod_reusable_usb`, with automatic GPS backup off. The current source includes the two-second LED pulse and button logic, but their physical approval/verification is not established by this build.

`v0.1.0-pilot` was **not** created or labelled deployment-ready: the GPS test integration defect and mandatory hardware gates remain unresolved. The manifest is explicitly a hardware-test preflight record, not an installable pilot release approval. There is no release tag, commit, merge, push, publication or deployment.

Before proceeding: correct/review the profile-observation defect and rerun appropriate regressions/builds; obtain explicit owner approval for the exact firmware upload; confirm idle/no export and preserve existing sessions using read-only diagnostics; perform up to three supervised GPS cycles with complete output and rate readback; verify post-wake recording admission, IMU, flash, button/LED and battery/standby limitations. Recording or flash-data modifications require separate explicit permission.

For a later approved flash, close the serial monitor, verify the exact package hash and same device identity, touch at 1200 baud or use the existing double-tap RESET loader entry, verify the new loader COM port, upload the application-only ZIP, then re-enumerate and monitor at 115200. Do not flash a bootloader, SoftDevice or mass-erase image. If USB does not recover, use documented double-tap RESET and inspect the loader identity; stop rather than trying unknown recovery. If GPS fails to wake, stop testing; firmware documents removing both USB and battery as the required power-cycle, which is an owner intervention and was not performed here.

## Outcome labels

- **PASS** — GitHub synchronisation and three firmware builds verified successfully.
- **FAIL** — Source integration cannot complete the backup test's profile phase successfully.
- **INCONCLUSIVE** — All physical GPS, recording, LED/button, battery and standby behaviour remains unverified.
- **BLOCKED** — Hardware upload awaits resolution of the source blocker and explicit owner approval; pilot release gates are incomplete.
