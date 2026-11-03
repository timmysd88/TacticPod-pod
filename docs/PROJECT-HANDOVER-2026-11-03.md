# TacticPod handover — 3 November 2026

Preferred firmware: **PR #5 commit `3d4693207d459b1565af2e8b3955cd897f5d519c`, environment `tacticpod_reusable_usb`**. PR #5 is merged on GitHub. This backup branch descends from that exact commit and adds documentation/evidence only; firmware source and behavior are unchanged. Experimental MAX-M10S software backup is absent. Do not substitute PR #6 or any GPS-backup environment.

## Firmware package

Archived under `logs/pr5-field-candidate-20261103/tacticpod_reusable_usb/`:

| File | Bytes | SHA-256 |
|---|---:|---|
| firmware.bin | 202192 | e8015cd7f400a5fdfcad784d211d72482451468783c8c9160566bf5cb3f612d6 |
| firmware.zip, application-only DFU | 203012 | b6d37fcabadd341e7f732a6bb2af46054387b9ea8a08ca8955befc52c05334a5 |

The candidate manifest, 31-suite regression results, focused LED/button tests, reusable/stock builds and successful restoration evidence are archived alongside. Manifests retain their original absolute paths and reference some excluded build products; they are historical evidence, not promises that every referenced file is in Git. No rebuild/upload is needed to resume planning. No independent live MCU application-hash query exists; device identity relies on the verified upload provenance.

## PR #5 LEDs and physical controls

Power on: solid blue 1 s. Awake idle: blue 100 ms every 2 s. Actual recording start: solid green 1 s; recording: green 100 ms every 2 s. Clean stop: two green flashes, then idle. Trusted/calibrated low battery: two orange flashes every 5 s. Refused start: three red flashes; recorder fault: three red flashes repeating every 2 s. Ordinary standby: LEDs off. GPS fix does not change the LED pattern.

D0 hold 3 s starts preparation; wait for actual green recording indication and do not repeat the hold during GPS preparation. Stop: brief tap, release, begin a second hold within 5 s and hold for 3 s. D0 is not a physical power switch. Button starts run until stopped; the default serial duration of 120 s does not impose a 120 s limit on button recordings.

## Last completed hardware checks

Session **162 passed USB-connected recording-data validation**. Capture was **72.491556 s**, exceeding the intended approximately 60 s because of interactive stop timing. IMU: 7250 records at 100.003777 Hz; GPS RMC: 725 at 9.999978 Hz; GGA: 73 at 1.000012 Hz. Timestamps monotonic, UTC advancing, clean reason-0 footer, all 8048 offered records committed, zero loss/rejected/missed/full/CRC/incomplete counters. Owner confirmed start/recording and stop/idle LED patterns. Brief navigation fix loss was correctly represented: 697/725 RMC fixes valid.

All 14 earlier sessions remain byte-identical: 1, 3, 12, 58, 61, 74, 96, 101, 119, 122, 133, 137, 144, 159. Session 133's historical interrupted final block at 0x087000 remains unchanged. Latest canonical exports are saved under `logs/pr5-field-test-60s-20261103/`:

- `before-test.tpx3`, 721344 bytes, SHA-256 `9cdc66e807012d2330439fde8761e3137cc7a3431627269c856fd2974c39bb72`.
- `after-test.tpx3`, 806464 bytes, SHA-256 `cf43251fd301f1eac71b5bd85c2850d195de5f9c2741956554abb2fbcbfbb934`; contains all 15 sessions, including 162. Derived image SHA-256 `75be48ff3152051a9213a47cbe8f1237455bc6d683c2ae40e3722353ef552607`.

The original post-test GPS STATUS was refused during an automatic profile readback, which subsequently completed successfully. The collection helper continued read-only checks before surfacing the refusal; this procedure limitation is documented, not hidden. A separately approved read-only preflight then waited for GPS PROFILE END and obtained successful GPS STATUS: 38400 baud, valid NMEA, checksum_ok=11801 / checksum_fail=0, current RAM readback verified 100 ms / RMC 10 Hz / GGA 1 Hz / GPS+Galileo, valid fix with five satellites. COM8 identity was 2886:8044, serial `0E8A89B7289430BD`. Flash: five matching JEDEC `0B 40 18`, reference-valid matching 256-byte SFDP passes, SR1=00 / SR2=02. qualification=UNRESOLVED is informational. Final recorder idle, exports complete, queues drained, error=none, lock=0; serial was closed.

## Battery testing remains pending

No three-minute battery-only test has been run or approved by this backup task. Last BAT STATUS: ADC_UNVALIDATED, cell=0, calibrated=0, valid=1, trusted=0, nominal 4113 mV / filtered 4123 mV, USB present, no ADC faults. This is not reliable charge/capacity/battery-presence evidence. Firmware low-voltage recording protection exists but is **unarmed**; unplugging USB will not arm it. Defaults: warning 3.6 V, stop 3.5 V, hard 3.3 V, recovery 3.7 V. It requires explicit cell mode, calibration and fresh samples; its action is a clean recording stop, not electrical disconnection. Hardware battery protection has not been established.

Remaining risks: earlier session-133 battery-only interruption unexplained; battery charge/condition/wiring, USB-to-battery transition and three-minute endurance unverified. Ordinary System ON standby suspends the MCU GPS UART, IMU and flash after unplugging; button wake requires a fresh GPS audit before capture. This is separate from experimental GPS software backup.

Next: review reports, physically check battery connection/polarity/condition and independently verify voltage/charge; decide whether separately approved calibration/arming is needed. Obtain explicit approval for any setting change or supervised battery-only test. Preserve recordings before further uploads. No automatic continuation, erases, experimental recovery, firmware changes or additional recording are authorized by this handover.

## Backup layout and local-only material

- `backup/pr5-field-handover-20261103`: this handover, PR #5 source unchanged, approved BIN/ZIP, latest canonical recordings, curated root scripts/reports/test evidence.
- `backup/gps-experiment-paused-20261103`: separate snapshot of substantive `.p6` changes/new diagnostic files and experiment reports. Never merge it into the field baseline without review. Original `.p6` and long-path worktrees remain untouched.
- Original root stays on main at `155a999122f0ce85ab6817c6eba690e9501fe4b3`; main is not advanced or merged by this task. Root untracked originals remain because copies were committed from `.p5`. Existing tracked line-ending/cache differences were not reset. Consult `logs/shutdown-20261103/*-initial-status.json` and final verification.
- `logs/shutdown-20261103/preserved-files.json` hashes curated source evidence. `local-only-files.json` lists excluded files and hashes: toolchains/caches, old generated firmware/ELF/HEX, duplicate recording exports/images, source snapshots, and large decoded JSON. Original per-test SHA256SUMS may reference these excluded originals. Derived images/decoded session JSON can be reconstructed from retained TPX3 using the committed decoder; historical unique old binaries/cache bytes are not all recoverable if the disk fails. No cloud-sync assumption is made.
- Archived scripts contain previously authorized upload/hardware actions and original absolute paths. Review them; do not run them automatically on resume.

## Exact resume commands (PowerShell)

On this PC, open the preserved field worktree without switching main or disturbing `.p6`:

```powershell
Set-Location 'C:\Users\timot\OneDrive\Documents\Projects\TacticPod_Prototype'
git fetch origin
git -C .p5 status --short --branch
git -C .p5 log -1 --oneline
git -C .p5 diff 3d4693207d459b1565af2e8b3955cd897f5d519c HEAD -- src platformio.ini variants tests tools
code .p5
```

The diff above should be empty. `.p5` holds the field backup branch, `.s6` the paused experimental backup; inspect `.s6/docs/PAUSED-GPS-EXPERIMENT-2026-11-03.md` read-only if needed. No device command is part of resuming.

On a replacement PC:

```powershell
git clone --branch backup/pr5-field-handover-20261103 https://github.com/timmysd88/TacticPod-pod.git TacticPod_Resume
Set-Location TacticPod_Resume
git log -1 --oneline
Get-FileHash logs/pr5-field-candidate-20261103/tacticpod_reusable_usb/firmware.bin -Algorithm SHA256
Get-FileHash logs/pr5-field-candidate-20261103/tacticpod_reusable_usb/firmware.zip -Algorithm SHA256
code .
```

If separately needed, preserve the experiment alongside with `git worktree add -b review/gps-paused .p6 origin/backup/gps-experiment-paused-20261103`. Reconstruct a clean exact firmware source checkout with `git worktree add --detach .field-source 3d4693207d459b1565af2e8b3955cd897f5d519c`. Neither command uploads firmware. Confirm branch/hash verification from the shutdown result before closing this session.
