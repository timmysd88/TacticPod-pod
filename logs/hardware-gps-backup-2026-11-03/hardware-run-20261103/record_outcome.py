"""Report the failed read-only preflight; never connects or uploads."""
from pathlib import Path
import json
import re

root = Path(__file__).resolve().parents[3]
out = Path(__file__).resolve().parent
pre = json.loads((out / 'preflight-result.json').read_text())
raw = (out / 'before-upload.tpx3').read_bytes()
identity = pre['identity']
assert re.findall(r'FLASH ID JEDEC \d+ ([0-9A-F]+)', identity) == ['000000'] * 5
result = dict(outcome='BLOCKED', phase='before_upload', explicit_upload_approval_received=True,
    candidate_verification='PASS', corrected_binary_sha256=pre['candidate']['binary_sha256'],
    zip_sha256=pre['candidate']['zip_sha256'],
    device_usb_identity='COM8 2886:8044 serial 0E8A89B7289430BD; matches prior XIAO Sense evidence',
    fresh_sense_bootloader_inspection=False,
    external_spi_flash_identity='FAIL: five JEDEC reads 000000; SFDP and status reads all zero; qualification UNRESOLVED',
    session_backup='BLOCKED: firmware returned REFUSED: JEDEC, not TPX3',
    captured_refusal_prefix=raw.decode('ascii', 'replace'),
    recordings_read_back=False, existing_recording_integrity='INCONCLUSIVE',
    recorder_idle_checks='PASS: V1 STOPPED; OBS/fast IMU OFF; COMP idle; TCB state=1 lock=0 error=none',
    preexisting_firmware_gps='VALID checksum NMEA at 38400; CURRENT RAM 100ms/RMC10Hz/GGA1Hz verified; NO FIX at observation',
    upload_attempted=False, firmware_uploaded=False, bootloader_entered=False,
    gps_backup_command_sent=False, gps_cycles=[], recording_or_erase_commands_sent=False,
    automatic_gps_standby_enabled=False, post_preflight_firmware_modifications=False,
    serial_released=pre['serial_released'],
    owner_intervention='Inspect external SPI flash power and connections; authorize retry of preflight when ready',
    approval_remains_scoped_to_original_corrected_candidate=True)
(out / 'hardware-result.json').write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')

report = root / 'docs/hardware-gps-backup-test-report.md'
text = report.read_text(encoding='utf-8')
text = text.replace('**Local recovery-gate correction; hardware tests await explicit upload approval.**',
    '**Corrected candidate approved for supervised testing; BLOCKED before upload by external SPI flash communication failure.**')
text = text.replace('## Connected hardware and flashing — BLOCKED pending approval',
                    '## Connected hardware and flashing — BLOCKED by failed preflight')
text = text.replace('No serial port was opened, no command sent and no device reset/upload performed during this correction.',
    'During the correction itself no device was opened. After the owner explicitly approved this exact candidate, the read-only preflight below opened COM8 at 115200 and queried status/flash identity. No 1200-baud touch, reset, bootloader entry or upload was performed.')
text = text.replace('No hardware command transcript exists yet. After approval, run `GPS BACKUP TEST` only while idle/no export and fresh NMEA/10 Hz verification are established.',
    'No GPS backup cycle transcript exists because the run stopped before upload. The complete preflight serial transcript and captured refusal prefix are saved below. Approval has been received for the corrected candidate; once the flash communication issue is resolved, repeat the non-destructive preflight before the authorized upload. Run `GPS BACKUP TEST` only while idle/no export and fresh NMEA/10 Hz verification are established.')
text = text.replace('Actual upload still requires explicit owner approval for the selected environment and exact candidate hashes. No approval to flash is inferred from permission to correct the source.',
    'The owner subsequently explicitly approved this exact candidate for supervised testing only. Upload was not attempted because the external SPI flash preflight failed. The existing approval remains scoped to this candidate; no additional firmware changes or uploads were performed after the failure.')
text = text.replace('**BLOCKED** — Device upload requires explicit approval; pilot preparation requires the outstanding hardware gates.',
    '**BLOCKED** — External SPI flash did not identify/respond correctly, so upload and all three GPS cycles were stopped before flashing. Pilot preparation still requires the outstanding hardware gates.')
text = text.replace('physical identification, partition and existing sessions not read.',
    'preflight identification FAILED (JEDEC 000000, all-zero SFDP, qualification UNRESOLVED); session export refused JEDEC. No recording contents could be verified.')
text += '''

## Approved hardware run: blocked before upload

The owner explicitly approved flashing the corrected `tacticpod_reusable_usb_gpsbackup` candidate to COM8 for supervised testing only. The following checks were performed before any upload:

| Check | Result |
| --- | --- |
| Device | COM8, VID:PID 2886:8044, serial 0E8A89B7289430BD; matches the previously identified XIAO Sense. No fresh loader/Sense board-ID inspection was performed because loader entry was not attempted. |
| Corrected source | PASS: base commit and all five modified source/test file hashes match the test manifest; all remaining firmware/variant/config content matches the verified PR. |
| Candidate binary | PASS: SHA-256 `aced1874981173ebfbb3904390f6088214aca3419f72f6a5fae0deb4f4c14051`. It differs from the original PR binary. ELF converted independently to BIN matches this exact package payload. The helper was inlined (no standalone helper symbol); provenance comes from the recorded build, source hashes, ELF/BIN identity and artifact manifest, not a claimed runtime helper inspection. |
| DFU ZIP | PASS: SHA-256 `91727831db63198d2c007134803526125a096adc47eeecbe1d8c3879e2004e28`; application-only, existing S140 requirement 0x0123. No bootloader/SoftDevice image and no external SPI flash erase operation are included in the planned upload. |
| Recorder/capture idle | PASS before upload: V1 STOPPED; OBS and fast IMU OFF; COMP capturing=0/fault=0, queues empty; TCB state=1, error=none, lock=0. No start, stop or erase command sent. |
| GPS on the pre-existing firmware | VALID checksum NMEA at 38400, current RAM readback verified 100 ms / RMC 10 Hz / GGA 1 Hz, checksum_fail=0; NO FIX at observation. This is not a test of the corrected candidate or GPS standby/wake. |
| Battery status | VBUS=1, cell=0, calibrated=0, trusted=0; unvalidated nominal ADC around 4.113 V. No voltage accuracy or battery-presence inference. No battery mode/calibration changed. |
| External SPI flash | FAIL: all five JEDEC reads returned `000000`; status, REMS and both SFDP passes were all zero, qualification UNRESOLVED. Repeatability reported PASS for the identical zero responses, which is not valid identification. |
| Preserve/read sessions | BLOCKED: the read-only `TCB READ` request returned `TCB REFUSED: JEDEC...`, not TPX3. The captured 20-byte prefix is preserved; it is not a valid session backup. Existing session/data integrity remains INCONCLUSIVE. |
| Upload / GPS cycles | NOT RUN: no reset, loader entry, firmware upload or `GPS BACKUP TEST` command. Cycles 1–3 and post-cycle recording readiness remain INCONCLUSIVE. |

The serial port was closed after the failed read-only preflight. No recording/erase command, automatic standby activation, firmware edit, recovery procedure, second upload, merge or push followed the failure. No conclusion about whether recordings are present, absent, damaged or erased can be drawn from the unavailable flash readback.

Evidence: `logs/hardware-gps-backup-2026-11-03/hardware-run-20261103/` contains `candidate-verification.json`, `preflight-result.json`, `before-upload-serial.txt` (full status/identity transcript), `before-upload.tpx3` (the invalid export/refusal prefix, not a backup), and `hardware-result.json`. The host preflight script was tightened to require the expected nonzero flash identity and resolved qualification; only this evidence/host tooling and the report were updated after the failure, not firmware.

Next required owner intervention: inspect the external flash module's power and SPI connections, then confirm when the device is ready for another non-destructive preflight. Do not infer a GPS backup failure: the GPS backup test was never run. The previously approved candidate and its checksums remain unchanged.
'''
report.write_text(text, encoding='utf-8')
print(json.dumps(result, indent=2))
