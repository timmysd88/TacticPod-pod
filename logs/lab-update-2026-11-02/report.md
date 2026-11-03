# Lab 0.5.2 update and local sync

PR: https://github.com/timmysd88/TacticPod-lab/pull/14
Merged under owner's explicit authorization on 2026-11-02.
GitHub main and both local Lab checkouts: b7efcae43164b8eb49ebfc940592c05927602985.
Tested source tree and merged source tree both: 5878651a2da301818866519a11f47f79491348fc.

Permanent VS Code source folder:
C:\Users\timot\OneDrive\Documents\Projects\POD LAB\TacticPod-lab

Compatibility changes: observed GPS preparation/capture/finalisation lifecycle;
strict STATUS response matching; physical-button control refresh; until-stop
mode and native-approved unlimited duration; free append capacity; separate GPS
snapshot/history/readback; manual stopped-only FIELD EVENTS capture with JSON,
overwrite evidence and SHA-256 sidecar. Firmware lacks event end markers and a
pending flag in STATUS; those uncertainties are documented rather than hidden.

Battery rows use nominal_mV before gain and filtered_mV after firmware gain when
optional raw/corrected fields are absent. Invalid readings remain unavailable;
uncalibrated readings remain unverified. Current firmware has no charging-state
telemetry and this pod is uncalibrated, so no reliable percentage is displayed.
Lab now explains those blockers. No calibration or charging settings changed.

Validation: 139 local tests passed, including exact-hash production decoding of
the existing physical archive with interrupted session 133 and clean session
144. Actual archive was removed from the unpublished commit after automatic
approval review rejected publishing real navigation data. The corrected branch
contains no new physical archive; CI uses the existing synthetic example to
exercise mixed clean/interrupted sessions. CI ran 138 tests successfully.

CI: https://github.com/timmysd88/TacticPod-lab/actions/runs/41455099602
All tests, Windows installer build, installed-app startup/SerialPort check,
uninstallation and user-data preservation checks passed. Artifact:
tacticpod-lab-windows-x64-d5dbf72672dfb8f6ce97751afa335442c0127f9e
Artifact ID: 13145569403; expires 2026-12-02.
ZIP digest reported by GitHub:
b997ba414819483a20815d0977ed8763a613fc2451b71a647612fe1dc00ff0ed
This is the artifact ZIP digest, not the installer executable checksum; the
artifact includes the installer and its generated SHA-256 sidecar.

Local source sync used fast-forward-only pulls. Permanent and development Lab
checkouts are on main and clean. Their pull.ff configuration is only.
Firmware project was pulled and verified against origin/main at
155a999122f0ce85ab6817c6eba690e9501fe4b3; src/lib/platformio.ini match remote.
Existing logs and working edits were preserved. Existing tracked log hash
before/after: 1cb5354ad1b3e95414142c05d287ba5f712b48f19d8229cc0f97b1996e8ac758.
Firmware checkout also uses pull.ff=only.

No device was accessed, flashed, erased, calibrated or recorded during this
Lab update. The installed desktop application was not replaced; install the
0.5.2 setup from the CI artifact to use these changes in the desktop app.
