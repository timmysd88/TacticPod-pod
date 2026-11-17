# GPS standby + BLE throughput integration

This is an uncommitted review candidate on `feature/idle-power-phase1`.
No physical pod connection, reset, upload, recording erase, commit, push,
branch merge or deployment was performed. Installed framework sources and
recording formats are unchanged.

## Protected starting point and integration method

The starting branch was verified at `81abe0514d906a7cc997d7a3c705c704830acd56`
with 22 existing uncommitted files. The prior Phase 1 validation contained
47 passing suite results after its documented corrected reruns; all recorded
normalized source hashes still matched.

Before fetching/integrating, an immutable ZIP was created and read back to
verify every original file byte-for-byte. It includes all 22 uncommitted files,
Git working-tree/index patches, the original three firmware builds, and prior
validation evidence (97 preserved files):

- Path: `.pio/idle-power-throughput-preservation/phase1-before-integration-20261117T125133Z.zip`
- SHA256: `d16881b15f9fa3dd2a3e3a7d7baffa90c287a1b75ab638077e0d0824960f5e7e`

Remote main was fetched. [PR #22](https://github.com/timmysd88/TacticPod-pod/pull/22)
was merged as `e92e885849b54e25c49f9e60a0de218c299cf2f6`. That main tree is
byte-identical to reviewed commit `8fee73507673121c126ba36a14593863236a3fe6`
(the squash merge has different commit ancestry). The complete latest-main
throughput implementation was therefore used.

All 16 upstream changed files were prepared in an ignored preview directory.
Files without local changes were checked against the original base before
accepting upstream content. Shared files used three-way text comparison;
`main.cpp` had one overlapping BLE-loop hunk. It was resolved explicitly as:

```cpp
BatteryRuntime::update(BatteryRuntime::monitor().s.cell);
delay(BatteryRuntime::idleDelayMs(BleRuntime::downloading() ? 1 : 10));
```

This preserves ADC completion/draining and the 1 ms active-download pace.
The BLE field-test mock now reflects the exporter download owner and supports
a controlled pacing assertion. The runner includes the combined suite and
stops immediately at the first failing suite. HEAD and index remain unchanged;
there was no Git branch merge, checkout, rebase or cherry-pick. All other
original Phase 1 files remain byte-for-byte identical to the backup.

## Preserved behaviour

- GPS PMREQ software standby, supported-firmware guard, UART edge wake,
  bounded 38400/9600 recovery, RAM-only baud restoration, fresh 10 Hz profile
  verification, failed-recovery refusal and explicit retry are retained.
- ADC phase/burst pacing, timeout, freshness, calibration and low-battery
  recording-admission checks are retained. Cell mode/calibration remain RAM-only.
- Eight notification entries and counted atomic credits are restored, including
  multi-completion events, resource backoff and credit overflow detection.
- Legacy window 4, explicitly negotiated windows 8/16, ordered framing,
  selective retry, CRC/ACK, recovery, cancellation and final confirmation remain.
- Reviewed bounded streaming bursts and 1 ms active-download scheduling remain.
  MTU 247, DLE 251, 2M PHY and connection interval requests use the upstream
  bounded negotiation/fallback implementation.
- Factory FICR identity, the read-only identity GATT characteristic, USB,
  recording controls, external-flash ownership/deep sleep and storage safeguards
  are preserved. GPS is not serviced by the active BLE main-loop branch.
- Default and reusable USB application binaries are byte-identical to the
  saved Phase 1 builds; the BLE application includes both feature sets.

## Validation procedure

All three compile-only builds completed successfully. The new combined suite
passed all three cases before the complete runner started: BLE startup/GET
keeps GPS asleep, repeated GET/cancellation keeps GPS asleep, and the main
loop uses 1 ms download / 10 ms idle pacing with the ADC guard.

The complete runner now contains 48 suites (the previous 47 plus
`tests/reusable/idle_throughput_tests.py`). It includes the reviewed queue,
credit, windows 8/16, negotiation, backpressure, recovery and cancellation
checks; the preserved GPS/ADC/admission/recording/USB regressions still run.
It is invoked with the existing pinned dependencies on `PYTHONPATH` and
bytecode generation disabled. No dependency installation or device access is used.

```
pio run -e seeed_xiao_nrf52840_sense
pio run -e tacticpod_reusable_usb
pio run -e tacticpod_reusable_ble
$env:PYTHONPATH = Join-Path $PWD '.pio-core/test-python'
$env:PYTHONDONTWRITEBYTECODE = '1'
python -B tests/reusable/run_all_tests.py
```

Final result: **48/48 suites passed**, exit code 0, on a single complete
fail-fast run; no failing suite was retried. All three builds passed.
The new combined suite passed all three cases in that complete run.
Logs: `.pio/idle-throughput-build-*.log`, `.pio/idle-throughput-targeted.log`,
`.pio/idle-throughput-regressions.log`, `.pio/reusable-regression-logs/`.

## Exact rollback search and alternative recovery

Required exact previously uploaded ZIP SHA256:
`dd60a0ecd26b4cd07323985ee8ea5c64e85258f5594c9c3c1352e73fced9d87c`.

No exact match was found in the searched local firmware/build archives,
rollback/staging directories, Downloads/Desktop/Documents or diagnostics
backups (35 ZIP files and three nested ZIPs were checked). The supplied hash
was also not found in the searched local upload/preflight records.

The successful reviewed-commit [CI run 44064480970](https://github.com/timmysd88/TacticPod-pod/actions/runs/44064480970)
contains artifact 14322058681. It was downloaded without triggering a new run
and verified against GitHub's artifact digest:
`f158c3f703758bb7da10da747225238f688fcf0147d24b00d01ff7ea044cee62`.
Its original inner BLE ZIP was preserved unchanged and matches its archived
`SHA256SUMS.txt`:

- Alternative path: `.pio/verified-rollback/alternative-reviewed-throughput-ci-8b3120d3.zip`
- SHA256: `8b3120d37752e4da307b0579994b626640acae5d10b35a953aeb1582aaf54b0b`
- Manifest: application-only; no bootloader or SoftDevice payload.
- Status: verified archive integrity/provenance, **not** the requested exact
  rollback and **not** proof it was uploaded successfully to this pod.

There was one Actions run for the reviewed head and none for the merge commit;
no other exact-match package was available there. Verification evidence is in
`.pio/verified-rollback/verification.json`. No package was reconstructed or
rebuilt and labelled as the known-working rollback.

Safe recovery plan: first recover the original `dd60...` ZIP from the machine
or archive used for its upload and verify the complete hash. If unavailable,
retain the existing on-device firmware and separately review/qualify the
archived CI alternative on a spare/test unit before adopting it as a recovery
image. Before any later live-device operation, preserve complete recording
exports and validate their contents. No recording reset/erase is part of recovery.
The missing exact rollback must be explicitly resolved in a separate upload
preflight; passing host tests does not waive that hardware recovery gate.

## Remaining hardware qualification

The BLE-only linker retains the reviewed 32 KiB SoftDevice reservation and
2 KiB ISR-stack reserve. The integrated BLE ELF has 79,248 bytes between heap
base and heap limit; this is an address margin, not a runtime high-water mark.
Actual SoftDevice RAM acceptance during BLE startup, heap/task-stack peaks,
negotiated MTU/DLE/PHY/interval and sustained throughput remain unmeasured here.

GPS standby current (including breakout overhead), UART pin transitions,
wake/baud/profile timing, actual 10 Hz GPS with recording, battery ADC trust,
USB reconnection and repeated sleep/wake/transfer cycles need supervised
hardware qualification. System OFF and physical power isolation are not added.

## Files changed by this integration

The previous Phase 1 changes remain in the working tree. This integration adds
or updates these files:

- `docs/ble-high-speed-design.md`
- `docs/ble-session-download-protocol.md`
- `linker/ble_throughput.ld`
- `platformio.ini`
- `src/ble_high_speed_window.h`
- `src/ble_performance.h`
- `src/ble_runtime.cpp`
- `src/ble_runtime.h`
- `src/main.cpp`
- `tests/reusable/ble_elf_tests.py`
- `tests/reusable/ble_field_integration.cpp`
- `tests/reusable/ble_gatt_tests.py`
- `tests/reusable/ble_protocol.cpp`
- `tests/reusable/ble_transport.cpp`
- `tests/reusable/ble_transport/bluefruit.h`
- `tests/reusable/ble_transport_tests.py`
- `tests/reusable/run_all_tests.py`
- `tests/reusable/idle_throughput_integration.cpp`
- `tests/reusable/idle_throughput_tests.py`
- `docs/idle-power-throughput-integration.md`

The final validation artifact records the complete working-tree inventory,
including every preserved Phase 1 file.

## Final build results and candidate

| Environment | Static RAM / available | Flash / available | Result |
| --- | ---: | ---: | --- |
| seeed_xiao_nrf52840_sense | 119,000 / 237,568 B | 167,964 / 811,008 B | PASS |
| tacticpod_reusable_usb | 123,468 / 237,568 B | 224,288 / 811,008 B | PASS |
| tacticpod_reusable_ble | 148,072 / 229,376 B | 340,604 / 811,008 B | PASS |

Integrated BLE candidate (not uploaded):
`C:\Tacticpod\TacticPod-pod\.pio\build\tacticpod_reusable_ble\firmware.zip`

SHA256: `1ad514561794458b805bb41ee34af369891916c4067bff964b52f1863c498740`.

ZIP CRC checks pass. Its application-only binary matches both the ELF's
clipped flash-load segments and the HEX application address range byte-for-byte.
ELF SHA256: `fa541f4464f006daf598d6fe3bdc0385ae3dd88093001b90a4952dbdf037c2be`.
HEX SHA256: `fde2ccd825ed1ad8a4c0ab7ba6e4e9b05a3eaa526a5dca71c54dcd888e19d06a`.

Final branch: `feature/idle-power-phase1`; HEAD remains
`81abe0514d906a7cc997d7a3c705c704830acd56`. The working tree has 30 modified
tracked files and nine untracked files (39 total), with an empty index and no
merge in progress. `git diff --check` passes. Of the original 22 uncommitted
files, 19 remain byte-identical to the preservation ZIP; only the explicitly
combined main loop, field harness and suite runner changed. All 14 other
upstream throughput files match fetched main byte-for-byte.

Complete results, source hashes, build hashes and the full 39-file inventory:
`.pio/idle-throughput-validation.json`. The original Phase 1 validation is
retained as historical evidence. No source changes followed these builds/tests;
only this report and the ignored final evidence file were finalized afterward.

The software candidate is ready for a **separate upload preflight**. This is
not upload approval: the unavailable exact known-working rollback must be
recovered, or an alternative recovery image must first be independently
qualified. The hardware checks above remain outstanding.
