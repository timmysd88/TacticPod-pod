# TacticPod PR #5 field-test candidate

Status: **software validation PASS**. Both builds, focused LED/button checks, linked startup and all 31 regression suites passed. Exact source and .p6 preservation audits passed. No device was contacted or uploaded, no physical recording, erase or hardware test occurred, and no push, merge, tag or release was performed.

## Exact source and isolation

Detached isolated worktree `.p5`, exact commit **3d4693207d459b1565af2e8b3955cd897f5d519c**, environment **tacticpod_reusable_usb**. Its parent is 155a999122f0ce85ab6817c6eba690e9501fe4b3. No source patch or cherry-pick was applied. Tracked source bytes and pinned library exports match Git LF blobs. Dependencies were copied into `.p5/.pio-core`; `.p6` was only read for local cached dependencies and preservation hashes.

The Windows system Git setting initially converted an archive to CRLF. The byte-exact fixture checks exposed this, the initial run was stopped, and all project/library files were restored from explicit `git -c core.autocrlf=false archive` output. The final builds/regression run use the exact LF source. Initial CRLF logs are retained separately; they are not counted as a successful validation run. No system/global Git configuration was changed.

Libraries: LSM6DS3 e063c3a544b8bb89238accddf24f0028bcd1ca20; TinyGPSPlus ca29434514a5c5172bd807af0608df7f296582a2. Seeed platform a6fd41debbbcda13b5340fff50c9092ed3289194; PlatformIO 6.2.0. Local Git object ownership trust was scoped to the relevant read-only command, not global configuration.

## Full PR #5 LED behavior

- Power-on: schedules a one-second solid blue event.
- Awake ready/idle: blue 100 ms pulse every two seconds, start-to-start.
- Recording begins: solid green for one second.
- Recording: green 100 ms pulse every two seconds, independent of GPS fix.
- Clean committed stop: two green 100 ms flashes, then ready pattern.
- Trusted/calibrated low-battery warning: two orange 100 ms flashes every five seconds, replacing ready/recording pulses. No calibration or warning-threshold changes were made.
- Start refused: three red 100 ms flashes.
- Persistent recorder fault: red triple flashes repeating every two seconds.
- Ordinary System ON standby: all LEDs off. D0 remains a recording control, not a physical power switch.

Priority: fault, one-shot events, low-battery warning, ready/recording pulse. Timing is non-blocking. Actual physical LED appearance has not been tested in this preparation.

## Compatibility and GPS backup exclusion

The only files changed from 155a999 are src/status_led.h, the LED-related calls in src/main.cpp, tests/reusable/button_led.cpp and docs/reusable-prototype.md. Fifteen GPS/profile, recorder, allocator, physical-button and export/decoder files compare byte-identically to the parent (listed in manifest.json). Main-loop GPS/IMU acquisition and storage service were not changed by this PR.

GPS 10 Hz / UART 38400 support, IMU 100 Hz capture, SPI append recording, button gestures, TPX3 export and TCB3 3.2.0 format remain unchanged. No flash migration is required. Existing interrupted session 133 remains historical evidence and must not be erased to hide its issue.

Experimental MAX-M10S software-backup code, test command, experimental environments, latch and diagnostic IRQ alias are absent. The selected BIN contains no GPS BACKUP TEST command. Ordinary UART parking / IMU and external-flash standby remain present; this is not MAX-M10S software backup. The stock-control BIN is byte-identical to the previous verified stock baseline.

## Artifacts

Application BIN: tacticpod_reusable_usb/firmware.bin, **202192 bytes**.
SHA-256: **e8015cd7f400a5fdfcad784d211d72482451468783c8c9160566bf5cb3f612d6**.

Application DFU ZIP: tacticpod_reusable_usb/firmware.zip, **203012 bytes**.
SHA-256: **b6d37fcabadd341e7f732a6bb2af46054387b9ea8a08ca8955befc52c05334a5**.

The package contains only application BIN/DAT and DFU metadata; it requires existing S140 0x0123 and contains no bootloader/SoftDevice update. The embedded BIN equals the extracted linked ELF application. The normal application upload updates internal MCU flash and does not intentionally erase external SPI recordings.

Both reusable and stock-control builds PASS. Focused LED/button and linked core/variant startup checks PASS. Full baseline 31-suite run: **PASS, 31/31, exit code 0**, without firmware/test-source patches or linker adapters. See regression-summary.json and regression-logs/. This candidate is not hardware-verified or approved for installation yet. Manifest and SHA256SUMS.txt cover both builds' BIN/ZIP/HEX/ELF outputs, project source hashes and exact dependencies.

## Safe upload procedure ? proposed only, requires approval

1. Approve these exact BIN and ZIP hashes. Recheck the files immediately before use; do not rebuild or substitute another artifact.
2. Confirm stable USB power, released button, the same XIAO on COM8 (2886:8044, unique serial 0E8A89B7289430BD), recorder stopped, OBS/COMP captures off, exports inactive and no pending/fault-locked session.
3. Confirm GPS has checksum-valid NMEA at 38400 and current 100 ms / RMC 10 Hz / GGA 1 Hz readback. If unresponsive, stop; uploading the MCU application does not guarantee waking an already sleeping GPS. Any owner power-cycle/recovery is a separate action, not an experimental command to attempt automatically.
4. Confirm five JEDEC 0B4018 reads, valid repeatable 256-byte SFDP matching the saved device reference, SR1=00/SR2=02. qualification=UNRESOLVED is informational. Stop on communication/status changes.
5. Export a complete current TPX3 backup, validate packet CRC/sequence and session/block/footer integrity, reconstruct the flash image and record hashes/session inventory. Prefer two matching exports. Record the known session-133 issue explicitly and stop on new/unexplained damage.
6. Close serial, use the existing approved bootloader entry, confirm the same unique device in loader mode (historically COM7, 2886:0045), and upload the archived application-only ZIP once using Adafruit nrfutil serial DFU at 115200 with --singlebank. No erase/mass-erase, bootloader or SoftDevice image.
7. Reopen COM8 at 115200. Confirm normal startup, stopped/idle recorder, flash/SFDP, valid NMEA and current GPS profile; a stopped GPS RATE window can check actual 10 Hz cadence. A read-only IMU preview checks communication, not the 100 Hz recording rate.
8. Export recordings again and compare the reconstructed image and integrity inventory with the pre-upload backup. No new recording should exist. Confirm the ready blue pulse at two-second intervals while awake; no automatic recording at boot.
9. Only after these pass, seek separate approval for one short USB-connected button recording/export test. Verify one session, clean stop, 100 Hz IMU / 10 Hz RMC / 1 Hz GGA timing, expected LED patterns, no new faults and old recordings preserved. No such test is authorized or performed by this preparation.

The prior battery-only interruption remains unresolved. This LED PR does not repair that issue or qualify untethered field operation; start with the USB-connected validation above. No present device/GPS/flash readiness is inferred from these offline checks.


## Subsequent approved restoration

The exact candidate was uploaded once on 2026-11-03. Startup, GPS/configuration, flash and full recording preservation checks passed; the owner confirmed the idle blue pulse every two seconds. Ready for a separately approved USB recording test, which has not run. See [restoration report](restore-20261103/REPORT.md) and its complete evidence. Original build manifest remains immutable.
