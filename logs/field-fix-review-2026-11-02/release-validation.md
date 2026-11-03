# Field-trial fix release validation

PR: https://github.com/timmysd88/TacticPod-pod/pull/4
Commit: 0b4f58719c40143f5cf996deedc727b49462ff92

Clean pinned tacticpod_reusable_usb and default builds passed. Application flash 202312/811008 bytes; RAM 122096/237568 bytes. BIN extracted from application-only DFU ZIP; all artifact SHA-256 values reverified against SHA256SUMS.txt.

All 31 regression suites have passing results: initial full run passed 29; console fixture lacked new GPS interface stubs, and download fixture hard-coded a boundary IMU count. Both test fixtures were corrected and rerun successfully, with original failed logs retained. See validation-results.json, tests-initial-full-run.txt, regression-logs and verified-rerun logs. Eight production-main-loop field cases passed. Final-source GPS and linked core/variant startup reruns passed. TCB3 store/format, battery runtime, corrected variant and PlatformIO configuration unchanged (preserved-source-checks.json).

GitHub CI runs separately and may still be in progress. No firmware upload, physical recording, erase, calibration or charging change performed for this fix. Source/PR release notes and instrumented retest: docs/field-trial-fixes.md. Hardware retest requires separate upload and recording approval. Receiver-profile loss physical cause and five observed starts remain unestablished.
