# PR #5 field-firmware restoration ? 3 November 2026

**PASS: one approved application upload; recording contents preserved; ready for a separately approved short USB-connected recording test. No recording test was performed. Serial is closed.**

## Exact installed application and authorization

Commit `3d4693207d459b1565af2e8b3955cd897f5d519c`, environment `tacticpod_reusable_usb`.

- Application BIN: 202192 bytes, SHA-256 `e8015cd7f400a5fdfcad784d211d72482451468783c8c9160566bf5cb3f612d6`.
- Application-only DFU ZIP: 203012 bytes, SHA-256 `b6d37fcabadd341e7f732a6bb2af46054387b9ea8a08ca8955befc52c05334a5`.
- Source, build manifest, all project/framework references and archived artifacts verified before preflight and again before upload. No rebuild or source modification. The immutable build manifest remains a build-time record; this restoration directory records the later owner authorization and installation.
- COM8 USB 2886:8044, unique serial `0E8A89B7289430BD`, matches the confirmed XIAO nRF52840 Sense. Loader COM7, 2886:0045, same unique serial. Existing bootloader retained; application returned on COM8.
- One normal 1200-baud bootloader entry and one Adafruit serial DFU process, --singlebank, exit 0, "Activating new firmware / Device programmed." Package contains application only and existing S140 requirement 0x0123. No bootloader/SoftDevice image, mass erase or intentional external SPI erase. No independent cryptographic readback of MCU application was performed; installation evidence is exact package verification, successful DFU and runtime behavior.

## Preflight and preservation

Recorders were stopped, OBS capture/fast IMU OFF, COMP capture 0 with empty queues, TCB state 1/idle, lock 0, error none, no export active. Flash passed five JEDEC 0B4018 reads, SR1 before/after 00, SR2 02, and both full 256-byte SFDP passes matched the saved device reference. qualification=UNRESOLVED was informational.

Two independently captured complete TPX3 backups passed sequence/packet CRC checks and reconstructed exactly the same 659456-byte image. They also matched the preserved pre-GPS-experiment image, SHA-256 `d1c398b03deee2e8ea88a86779e856154ab0dfbac4a1165df1f62faf678e0379`.

All 14 sessions were preserved: 1, 3, 12, 58, 61, 74, 96, 101, 119, 122, 133, 137, 144 and 159. Session 133 remains interrupted, with the known incomplete final block at 0x087000; all other listed sessions are complete. No new/unexplained recording integrity issue was found. This pre-existing issue was preserved, not erased or described as an entirely issue-free archive.

Pre-upload GPS had checksum-valid NMEA at 38400, but its RAM configuration snapshot was stale. As explicitly authorized, that GPS-only status did not block rollback after identity/package/flash/preservation checks passed. No experimental GPS recovery command was sent.

## Post-upload checks

Normal reusable application startup/menu at 115200 passed, with no GPS BACKUP TEST menu command. Recorder remained STOPPED; OBS/fast IMU OFF, COMP empty, TCB idle state 1/session 0/programs 0/lock 0/error none. Firmware RAM counters reset at application restart; this does not indicate recording loss.

GPS STATUS confirmed valid NMEA on Serial1 RX D7 / TX D6 at 38400 and current RAM readback verified: MEAS100ms / RMC10Hz / GGA1Hz / GPS+Galileo. This verifies communication/configuration, not a new recorded sample-rate measurement or a sky fix requirement. No GPS backup, scan, reset, restore or test command was run.

Flash identification/status/SFDP repeated successfully after upload with the same fingerprint. A third complete TPX3 backup passed transport CRC/sequence checks and reconstructed an image byte-for-byte identical to both pre-upload backups. The session inventory and known session-133 integrity issue were identical. No new recordings, session headers or footer changes were created.

The owner confirmed **a brief blue pulse about every 2 seconds while idle with the button released**, matching PR #5. Power-on and recording/start/stop/error/low-battery patterns were not physically exercised by this procedure. Their implementation is covered by the exact commit and prior focused tests; no recording test or battery-warning manipulation was used to probe them.

## Readiness and limits

**Ready for a separately approved short USB-connected recording test.** Such a test has not started and is not implicitly authorized by this restoration. It should check exactly one new session, physical button start/clean stop, green recording indication, 100 Hz IMU / 10 Hz RMC / 1 Hz GGA timing, integrity and old recordings preserved.

The earlier battery-only interruption remains unexplained. This rollback and LED change do not qualify untethered field operation or battery protection/calibration. No firmware modifications, experimental standby enablement, recording tests, erases, pushes, merges, tags or releases were performed. No further action was taken.

Evidence in this directory: candidate-verification.json, preflight-result.json, pre-upload-serial.txt, two before-upload TPX3/image/decoded/integrity backups, upload-attempted.json, dfu-invoked-once.json, upload.txt, upload-result.json, post-upload-serial.txt, post-upload-result.json, after-upload TPX3/image/decoded/integrity backup, hardware-summary.json and evidence-manifest.json. Binary export bytes are preserved in the TPX3 files; the serial text logs mark their locations.
