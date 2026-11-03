"""Seal the approved upload/failed cycle evidence. No device operations."""
from pathlib import Path
import hashlib
import json

root = Path(__file__).resolve().parents[2]
out = Path(__file__).with_name('hardware-run-approved-20261103')
upload = json.loads((out/'upload-result.json').read_text())
tests = json.loads((out/'test-result.json').read_text())
pre = json.loads((out/'preflight-result.json').read_text())
candidate = json.loads((out/'candidate-verification.json').read_text())
assert upload['uploaded'] and upload['exit_code'] == 0
assert len(tests['cycles']) == 1 and not tests['cycles'][0]['passed']
cycle = tests['cycles'][0]
assert 'FAIL - standby not confirmed' in cycle['response']
assert 'UART wake attempted: NO' in cycle['response']
assert tests['serial_released']
(out/'cycle-1-response.txt').write_bytes(cycle['response'].encode('utf-8'))
manifest = json.loads((Path(__file__).parent/'recovery-gate-fix/manifest.json').read_text())
assert all(hashlib.sha256((root/a['path']).read_bytes()).hexdigest() == a['sha256']
           for a in manifest['changed_source_and_tests'] + manifest['artifacts'])
summary = dict(outcome='FAIL', failure='Cycle 1: standby entry/silence not confirmed',
    approved_binary_sha256=candidate['binary_sha256'], zip_sha256=candidate['zip_sha256'],
    base_commit=candidate['base_commit'], local_patch_sha256=candidate['uncommitted_patch_sha256'],
    actual_dfu_process_invocations=1, firmware_upload='PASS', loader='COM7 2886:0045, matching unique serial',
    uf2_disk_exposed=False, fresh_info_uf2_read=False,
    initial_helper_stopped_before_sending_firmware=True,
    application_port='COM8', usb_baud=115200,
    external_spi_erase_intended=False,
    startup_flash_validation=tests['flash_startup']['validation'],
    startup_idle=True, startup_gps_nmea=True, startup_ram_10hz_verified=True,
    standby_request_uart_sent=True, standby_silence_confirmed=False,
    explicit_wake_attempted=False, wake_recovery_verified=False,
    time_to_valid_nmea_ms=None, time_to_valid_nmea_reason='Not measured: Wake phase never entered',
    post_cycle_10hz_verification='NOT RUN after failure',
    post_cycle_flash_verification='NOT RUN after failure',
    post_failure_recording_readiness='INCONCLUSIVE: no console readiness flag and no recording probe; stopped immediately',
    cycles_attempted=1, cycles_successful=0, cycles_2_and_3='NOT RUN',
    recording_or_erase_commands_sent=False, automatic_gps_standby_enabled=False,
    post_upload_recording_contents='INCONCLUSIVE: no post-cycle export after immediate stop',
    before_upload_recording_backup=pre['archive'],
    recovery_commands_sent=False, firmware_changes_after_failure=False,
    additional_uploads_after_failure=False, merge_push_or_release=False,
    serial_released=True)
(out/'hardware-summary.json').write_text(json.dumps(summary, indent=2)+'\n', encoding='utf-8')

report = root/'docs/hardware-gps-backup-test-report.md'
text = report.read_text(encoding='utf-8')
text = text.replace('**READY FOR FLASH APPROVAL — final read-only preflight passed. No upload or GPS backup test is authorized without further explicit approval.**',
    '**Approved corrected firmware uploaded successfully. Physical GPS backup cycle 1 FAIL: standby was not confirmed; testing stopped immediately.**')
text += '''

## Explicitly approved upload and supervised GPS backup test — FAIL

The owner explicitly approved uploading the exact corrected supervised-test candidate, BIN SHA-256 `aced1874981173ebfbb3904390f6088214aca3419f72f6a5fae0deb4f4c14051`, and up to three cycles only while each preceding cycle passed and fully recovered. No automatic GPS standby, recording/erase, merge, push or release was authorized.

### Before upload — PASS

Source/patch and artifact hashes were verified again. The package payload, saved BIN and independent ELF-to-BIN conversion all match the approved SHA-256. ZIP SHA-256 remains `91727831db63198d2c007134803526125a096adc47eeecbe1d8c3879e2004e28`. The ZIP contains only an application component, with existing SoftDevice requirement `0x0123`; there is no bootloader/SoftDevice image or intentional external SPI flash erase operation.

COM8 again matched `2886:8044`, serial `0E8A89B7289430BD`. Idle, fresh NMEA/verified GPS profile and flash-ID/SFDP checks passed. A fresh read-only recording download was byte-identical to the complete final-preflight image (including all stored sessions and the known historical interrupted session 133); idle was rechecked after export. No record/erase command was sent.

### Upload and startup — PASS

One 1200-baud loader entry moved the same device to COM7, VID:PID `2886:0045`, with the same unique serial. The initial helper stopped before invoking DFU because this serial-DFU entry exposed no UF2 disk/INFO_UF2 file. Read-only volume/serial inspection confirmed no mounted loader disk. No firmware bytes had been sent by that helper.

The already-entered serial loader was then used without another reset, matched against the confirmed application's unique serial and expected bootloader USB identity. The historical Sense identity remained the board provenance; no fresh INFO_UF2 read is claimed. Exactly **one** Adafruit `nrfutil dfu serial ... -b 115200 --singlebank` process sent the approved application ZIP. It exited 0 and reported `Activating new firmware` / `Device programmed.` The application returned on COM8.

At 115200 baud, the startup menu included `GPS BACKUP TEST`. Checks before cycle 1 confirmed:

- V1 STOPPED; OBS/fast IMU OFF; COMP idle, no fault, empty queues; TCB state=1/session=0/error=none/lock=0/programs=0.
- Five `0B4018` IDs, SR1 before/after `00`, SR2 `02`; SFDP header `53464450000101FF`; both complete 256-byte passes matched the verified reference.
- Checksum-valid NMEA at 38400, cumulative checksum failures 0, fresh message age 27 ms at observation; CURRENT RAM readback verified 100 ms / RMC 10 Hz / GGA 1 Hz. This is configuration verification, not a separately measured navigation-solution cadence.
- The selected environment enables the supervised command only. Automatic GPS backup remains disabled.

### Cycle results

| Check | Cycle 1 | Cycles 2–3 |
| --- | --- | --- |
| Standby request written to UART | YES | Not run |
| Silence after 3000 ms parked / within 1500 ms listen check | **NO — incoming UART traffic observed** | Not run |
| Explicit `0xFF` wake attempted | **NO** | Not run |
| Time to valid NMEA from first wake | Not measured; Wake phase never entered | Not run |
| Post-wake baud / 10 Hz / fix | Not checked; Wake/Profile/Fix phases not reached | Not run |
| Post-cycle flash and recording-readiness checks | Not run after failure; immediate stop | Not run |
| Overall | **FAIL — standby not confirmed** | Not run because cycle 1 failed |

Exact cycle output:

```text
GPS BACKUP TEST started (about 20 s, USB stays awake; recording blocked). Wake mask 0x8
GPS BACKUP TEST RESULT (wake mask 0x8)
  standby request sent: YES
  standby confirmed (UART silent 1500 ms after 3000 ms parked): NO (NMEA kept flowing; receiver did not enter standby)
  UART wake attempted: NO
  time until valid NMEA: NOT RETURNED
  GPS baud 38400: NOT CONFIRMED
  10 Hz configuration: NOT CHECKED
  GPS fix: NOT CHECKED
GPS BACKUP TEST: FAIL - standby not confirmed; receiver kept running (baseline power). Wake was not tested.
GPS BACKUP TEST END
```

Interpretation limits: `standby request sent` confirms the MCU UART accepted the request, not an acknowledged receiver acceptance. The state machine fails on incoming bytes during its listen phase. Its text says NMEA kept flowing, but the failure trigger itself is a byte-count change; a separate post-cycle checksum-valid NMEA/cadence check was not performed. This result cannot distinguish failure to enter standby from an early/unintended wake when UART was restarted. `NOT RETURNED` in this branch is not evidence of a failed attempted wake: **there was no explicit wake attempt**. No root cause or alternative wake mask is established by this test.

### Immediate stop and data safety

After the failed result, no more device commands were sent. No second/third cycle, baud scan, receiver reset, recovery command, flash diagnostic or additional upload was attempted. Serial was closed. The approved test firmware remains installed; firmware source/test/build hashes are unchanged. Automatic GPS backup stays off. No host recording, stop, erase, flash status/configuration write, merge, push or release was performed.

The pre-upload recording backup is preserved. A post-upload/post-cycle byte-for-byte export was not performed because the owner required immediate stop on failure. Do not claim physical post-upload session integrity or recording readiness was verified. Source handling of EntryNotConfirmed does not set the failed-wake latch, but the resumed UART's profile audit may still be pending; no recording start was used to probe admission.

Evidence: `logs/hardware-gps-backup-2026-11-03/hardware-run-approved-20261103/` contains `pre-upload-serial.txt`, the complete pre-upload TPX3/image/decoded backup, `candidate-verification.json`, `preflight-result.json`, loader/DFU intent records, `upload.txt`, `upload-result.json`, **`post-upload-and-cycles-serial.txt`** (full raw startup/checks/cycle transcript), `cycle-1-response.txt`, `cycle-1.json`, `test-result.json` and `hardware-summary.json`.

**Outcome: upload/startup PASS; physical supervised GPS backup test FAIL; wake recovery and subsequent cycles INCONCLUSIVE/not tested.** Experimental automatic GPS standby and pilot release are not qualified. Results are reported now, before any further firmware modifications or uploads, as requested.
'''
report.write_text(text, encoding='utf-8')
investigation = root/'docs/flash-detection-investigation.md'
text = investigation.read_text(encoding='utf-8')
text += '''

## Subsequent explicitly approved upload

The corrected supervised GPS test application was subsequently uploaded once under fresh explicit approval. Flash identification/SFDP matched the verified reference both before upload and at application startup. Thus the earlier external-flash communication failure was not reproduced in those checks. The first physical GPS backup cycle then failed its UART-silence/standby-confirmation check, before an explicit wake attempt; testing stopped immediately. No further flash diagnostics or session export were issued after that failure. See the hardware test report and `logs/hardware-gps-backup-2026-11-03/hardware-run-approved-20261103/` for the full transcript. This does not establish a causal relationship between the earlier flash fault and the GPS standby failure.
'''
investigation.write_text(text, encoding='utf-8')
print('Saved hardware summary and full cycle transcript; report updated: cycle 1 FAIL, immediate stop')
