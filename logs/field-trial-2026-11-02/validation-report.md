# First field-trial validation — 2026-11-02

Status: Approved Phase 4 application upload succeeded once. Post-upload USB diagnostics and recording-retention checks passed. Device idle on COM8; stopped after diagnostics as requested. No recording or calibration performed. Physical field/battery validation remains untested.

## Phase 1

- Initial working tree clean, branch main. Remote origin: timmysd88/TacticPod-pod.
- Local and GitHub main: b3b0f1c0c1db2e92d041e057ef4e63cee646b079. GitHub connector confirmed current main and PR #2 merged as 0a95d83b1761ec445e02ecb41601a38413fdf304; local ancestry verified. No pull needed. Direct git HTTPS was unavailable in the sandbox.
- Environment: tacticpod_reusable_usb (explicit selection required; default_envs is seeed_xiao_nrf52840_sense). Board ID seeed-xiao-afruitnrf52-nrf52840; Seeed_XIAO_nRF52840 variant with Sense Wire1 IMU build flag.
- Installed pinned toolchain unchanged: PlatformIO 6.2.0; Seeed platform a6fd41debbbcda13b5340fff50c9092ed3289194; Arduino framework 1.1.1; GCC ARM 7.2.1; adafruit-nrfutil 5.3. TinyGPSPlus ca29434514a5c5172bd807af0608df7f296582a2; LSM6DS3 e063c3a544b8bb89238accddf24f0028bcd1ca20.
- Clean reusable build passed; default resource-baseline build passed. Reusable RAM 120956/237568; application flash 194600/811008 bytes. Full logs in build.txt and clean.txt.
- Linked core/variant startup emulation passed, including inherited HIGH negative case and default-variant negative control. P0.14 LOW before OUTPUT; P0.13 INPUT/no pulls. These are software checks, not electrical measurements.
- Full regression runner: 28/30 suites passed; button_tests.py and standby_tests.py could not launch because host g++ is absent (WinError 2), rather than assertion failures. Both unchanged scripts subsequently passed with their original warning flags and assertions using installed ARM GCC 7.2.1 + Unicorn compiler/execution adapter, including standby source/hash checks. See arm-button-standby-checks.txt and run_arm_host_checks.py. No compiler installed, test edited or toolchain upgraded. Original nonzero runner result retained; see tests.txt and regression-logs/results.json.
- Container format TCB3 3.2.0; TPX3 export preserved. Source review: button D0/P0.02, 25 ms debounce, 3 s start hold, stop tap <=500 ms then release and second 3 s hold initiated within 5 s. Button starts until-stop mode; serial default is timed 120 s, configurable to 1–1800 s or UNLIMITED. Finalisation deadline 60 s; full-flash footer space reserved.
- GPS UART 38400, RAM-only 10 Hz RMC / 1 Hz GGA profile; recording IMU target 100 Hz, sensor ODR 104 Hz. Status LED blue/green for GPS, orange only with trusted calibrated battery readings. System ON standby polls D0, powers down IMU and NOR, suspends GPS UART/timer; GPS module remains powered. No measured standby current.
- Preserved artifacts: firmware.bin, firmware.hex, firmware.elf, firmware.zip; checksums in SHA256SUMS.txt. BIN extracted directly from application-only ZIP. No bootloader or SoftDevice component in ZIP manifest. HEX checksum matches PR #2's published checksum.

## Phase 2

Completed read-only preflight on COM8, VID:PID 2886:8044, serial 0E8A89B7289430BD, matching prior prototype logs. RECORD STOPPED, OBS capture/fast IMU OFF, compressor idle/fault=0/queues empty; TCB state=1, lock=0, error=none, programs=0; rejected/missed/full/CRC/incomplete=0. Current next append address 0x049000, superseding the supplied historic 0x03C000.

Flash JEDEC repeated 5 times = 0B4018; SR1 before/after 00, SR2 02; both 256-byte SFDP reads match the previously measured 16 MiB fingerprint byte for byte. END_FLASH_ID still labels qualification UNRESOLVED: identity verified against reference, no claim of broader electrical qualification.

Saved complete TPX3 export before-upload.tpx3, 327104 bytes / 1170 packets, SHA-256 40f599d7f0082e63ee6bd9f8baff3fff2cdd21cfec83af567280cfdcb69643ad. Reconstructed image in before-upload-image.bin; full decoder output in before-upload-decoded.json; raw transcript and preservation-result.json retained here, outside temporary build directories. Packet sequence/CRC, export issues, root issues and each session's issues checked; all sessions complete with clean footers, no lost/rejected/missed/full/CRC/incomplete counts.

| Session | IMU | RMC | GGA |
| --- | ---: | ---: | ---: |
| 1 | 500 | 50 | 5 |
| 3 | 3000 | 300 | 30 |
| 12 | 12001 | 1200 | 120 |
| 58 | 1000 | 100 | 10 |
| 61 | 4236 | 423 | 43 |

Session 61 is additional to the historic inventory and is preserved. No recording was started during this work. These counts describe pre-existing sessions, not a new hardware trial.

GPS current diagnostics: UART/NMEA valid, checksum_fail=0, 10HZ VERIFIED RAM profile, no current fix, satellites=0. Live IMU preview/rate measurement deferred to Phase 5. Battery: ADC_UNVALIDATED, cell=0, calibrated=0, valid=1, trusted=0, nominal_mV=4123, filtered_mV=4125, adc_timeout=0, invalid=0, usb_vbus=1. These are uncalibrated telemetry, not measured reliable cell voltage or armed low-battery protection. No BAT config/calibration sent.

## Phase 3

Source review: board upload protocol nrfutil, application DFU ZIP, 115200 baud, --singlebank. PlatformIO's configured upload uses 1200-baud touch then port discovery. After stopped-state verification, completed backup and serial release, one existing 1200-baud touch successfully entered serial DFU. Actual bootloader is COM7, VID:PID 2886:0045, serial 0E8A89B7289430BD. Board JSON lists 0044, so prefer the established direct adafruit-nrfutil application-ZIP command on the verified current port rather than rely on PlatformIO's port matching. No uploader invoked and no write attempted. See bootloader-inspection.json.

Owner demonstrated physical double-tap RESET recovery: XIAO-SENSE drive E:. Read and preserved E:/INFO_UF2.TXT: UF2 Bootloader 0.6.1, Model Seeed XIAO nRF52840, Board-ID Seeed_XIAO_nRF52840_Sense, SoftDevice S140 version 7.3.0, build date Nov 12 2021. Identity is consistent with the factory Seeed Sense bootloader; no bootloader replacement or write performed. COM7 rechecked: VID:PID 2886:0045, serial 0E8A89B7289430BD. Application package requires SoftDevice ID 0x0123, matching configured S140 7.3.0. Rechecked all preserved artifact hashes and TPX3 backup hash successfully. Device remains in bootloader; no application recording or binary transfer active. Upload remains gated on explicit approval.

Prepared build command (already executed):

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e tacticpod_reusable_usb
```

Approved upload command — executed once successfully after explicit approval:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" .pio-core/packages/tool-adafruit-nrfutil/adafruit-nrfutil.py dfu serial -pkg logs/field-trial-2026-11-02/firmware.zip -p COM7 -b 115200 --singlebank
```

Application DFU does not intentionally erase the external SPI NOR. Application startup performs read-only recovery; TCB3 store has no erase API and guarded reusable firmware blocks legacy write/erase commands. No full-chip erase, bootloader/framework change, charging-current change or calibration command is planned. RAM calibration and recording settings are lost on reset/upload. Bootloader recovery procedure documented as double-tap RESET to UF2 drive; this unit's INFO_UF2.TXT is now preserved and inspected.

## Phase 4 — approved upload

Explicit user approval received for verified application-only DFU on COM7, with recording/calibration prohibited. Immediately rechecked package SHA-256 `d90d678ed1840a123756b796bb6a4288df5239d32e0399ed3255b4b831d310ff`, BIN `b2ea6b3bab286b97ecc4235039944d1be335d07711c579952fe99dea120a4ddc`, application-only manifest, S140 requirement 0x0123, backup hash, COM7 identity and unchanged INFO_UF2.TXT.

One adafruit-nrfutil application DFU attempt: exit 0, “Activating new firmware” / “Device programmed.” Application reconnected on COM8, VID:PID 2886:8044, same serial 0E8A89B7289430BD. No retry, full-chip erase, external-flash erase, charging-setting command or calibration. Package had no bootloader/SoftDevice component; their bytes were not independently read back afterward. See upload.txt, upload-attempted.json and upload-result.json. Phase 3's no-upload statements above describe the earlier approval gate.

## Phase 5 — post-upload USB diagnostics

- Recorder STOPPED, OBS capture/fast IMU OFF, compressor idle/fault=0/queues empty. TCB state=1, session=0, mode=timed, default duration 120 s, lock=0, error=none, programs=0; rejected/missed/full/CRC/incomplete=0. Next append remains 0x049000.
- Flash JEDEC 0B4018 repeated five times, SR1=00 before/after, SR2=02; both SFDP reads byte-match the prior 16 MiB fingerprint.
- IMU: 20 live accelerometer/gyro readings in two seconds, varying values, no IMU error. This verifies sensor communication and 10 Hz diagnostic output; 100 Hz acquisition in a new recording was not measured.
- GPS: UART 38400, 10HZ VERIFIED RAM profile. In 15 s: RMC 149 (9.933 Hz), GGA 15 (1.000 Hz), checksum-valid 164, failures 0, overlong 0. No fix/satellites and no advancing UTC: valid 10 Hz navigation remains unverified.
- Battery ADC_UNVALIDATED: nominal 4123 mV, filtered 4118 mV, valid=1, calibrated=0, cell=0, trusted=0, adc_timeout=0, invalid=0, usb_vbus=1. Gain/limits unchanged. Uncalibrated telemetry is not qualified cell voltage or armed protection.
- after-upload.tpx3: 327104 bytes, 1170 packets, SHA-256 `40f599d7f0082e63ee6bd9f8baff3fff2cdd21cfec83af567280cfdcb69643ad`. Exactly byte-identical to before-upload.tpx3. Reconstructed recorded flash image also byte-identical. Sessions 1,3,12,58,61 all complete, counts unchanged, no additional session. Packet/export/root/session issues empty; all footer loss/rejected/missed/full/CRC/incomplete counters zero.
- Closed and reopened serial at 115200: recorder remained idle, programs=0, lock=0, error=none. USB serial reopening did not start recording. Physical USB detach/reinsert and off-USB standby entry/wake/current not tested.
- D0/P0.02 mapping confirmed by source/compiled tests and new firmware menu; physical button/LED behaviour not tested. Live IMU printing disabled before export; serial released with no transfer active.

Initial diagnostics stopped on an overly strict host assertion requiring free_sectors. Source review confirmed that the field is conditional on TCB scan state; a fresh idle store omits it. Corrected the host expectation and completed diagnostics without reflashing. Initial script/result/transcript retained as post_upload_initial.py, post-upload-initial-result.json and post-upload-initial-transcript.txt. Firmware unchanged.

Evidence: post-upload-result.json, after-upload-transcript.txt, usb-reopen-transcript.txt, after-upload-decoded.json, post-upload-completed-console.txt.

## Phase 6 — not performed

Stopped after diagnostics as requested. No recording or calibration, new sample-rate/duration result, physical button/LED test, thermal observation or standby-current measurement. Outstanding field checks: GPS fix/advancing UTC, button/LED sequence, separately approved battery calibration, standalone acquisition/finalisation, 100 Hz IMU recording rate, standby entry/wake/current and supervised power/thermal observations.
