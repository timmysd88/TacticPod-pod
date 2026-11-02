# PR #4 CI resource regression

CI run: https://github.com/timmysd88/TacticPod-pod/actions/runs/41438520621

The downloaded artifact's `regression-logs/tests_compact_test_resources.txt`
fails at line 17, `assert static<=119000`. This is the first assertion; the
compact-object and stack assertions were not reached in that CI invocation.
The CI default-build log reports 119008 RAM bytes. A fresh build with the same
pinned toolchain reproduces exactly 119008 bytes in `.data + .bss`, so this is a
real default-build static-RAM budget regression, not a corrupt binary or a
compiler/environment discrepancy. The terse original assertion omitted its
measured value; the test now prints measurements and includes them in failures.

The test measures **seeed_xiao_nrf52840_sense**, not the reusable field build.
The PR's approximately 122 KB field-build figure was not the failing measurement.
An earlier local result of 118992 bytes came from an older ELF; the later clean
default ELF had exceeded the cap. CI correctly caught the missing final check.

## Same-toolchain comparison

Main remains `b3b0f1c0c1db2e92d041e057ef4e63cee646b079`. Its source was built in
an isolated snapshot with the same installed packages and pinned libraries;
the working PR branch, device and existing archives were not changed to do so.

| Measurement, bytes | Main | PR before correction | PR after correction |
| --- | ---: | ---: | ---: |
| Default `.data` | 1380 | 1384 | 1380 |
| Default `.bss` | 117612 | 117624 | 117612 |
| Default static total | 118992 | 119008 | 118992 |
| Reusable static total | 120956 | 122096 | 122040 |
| Compact objects, either build | 37966 | 37966 | 37966 |
| Reusable diagnostic ring | 0 | 1040 | 1040 |
| Event-name table in reusable RAM | 0 | 56 | 0 |

Symbol comparison identifies three GPS-audit booleans (3 bytes), its readback
timestamp (4), its current-detail pointer (4), and 5 bytes of alignment as the
default build's 16-byte increase. These field-only variables are now compiled
only for TACTICPOD_REUSABLE; legacy/default profile operations retain their
existing behaviour. Field-readiness APIs explicitly remain unavailable there.

The reusable build's original increase was 1140 bytes: 1040 for the 64-event
ring (1024 payload + 16 bookkeeping), 56 for its event-name pointer table, and
44 for other field/GPS/start-admission state and alignment. The ring was never
present in the failing default build. Its name pointers are now const as well
as their strings, placing all 56 table bytes in read-only storage. All 64 event
entries, timestamps, names and overwrite reporting are preserved.

## Limits and validation

Limits are unchanged: default static 119000, compact objects 38000, measured
command/encoder stack 1024 bytes each. The corrected resource test passes with
118992 static, 37966 compact, command stack 456 and encoder stack 192 bytes.
The default budget again has its original 8-byte margin; it has not been widened.

Reusable linker allocation now leaves 113480 bytes of heap region plus a
2048-byte MSP stack reservation out of the 237568-byte application RAM region.
These are link-time reserves, not proof of whole-firmware runtime peak usage;
the isolated stack test excludes interrupts, RTOS tasks and heap peaks.

Both pinned application builds pass. GPS and eight production-main-loop field
regressions are rerun to check that the default isolation and read-only event
table retain profile recovery and diagnostics. CI must rerun on the correction;
no merge, upload, recording, erase, calibration or hardware modification is part
of this investigation. New artifact checksums are in the release notes; older
release bundles remain preserved as historical builds.

Local evidence: `logs/resource-review-2026-11-02/ci-artifact.zip`, the extracted
failed assertion/results, `resource-comparison-before.json`,
`resource-comparison-after.json`, `main-build.txt`, `after-build.txt`,
`resource-after.txt`, `gps-after.txt` and `field-after.txt`.
