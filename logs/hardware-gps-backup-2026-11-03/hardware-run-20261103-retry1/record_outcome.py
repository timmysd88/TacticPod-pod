from pathlib import Path
import json
import re

root = Path(__file__).resolve().parents[3]
out = Path(__file__).resolve().parent
pre = json.loads((out / 'preflight-result.json').read_text())
reads = re.findall(r'FLASH ID JEDEC \d+ ([0-9A-F]+)', pre['identity'])
assert reads == ['000000'] * 5
result = dict(outcome='BLOCKED', phase='read_only_preflight_retry_1',
    candidate_verification='PASS', serial_port='COM8',
    source_and_binary_unchanged=True, binary_sha256=pre['candidate']['binary_sha256'],
    external_flash_jedec_reads=reads, sfdp='all zero; qualification UNRESOLVED',
    recorder_idle=True, existing_firmware_gps='NMEA VALID at 38400, CURRENT RAM 10Hz verified; NO FIX',
    session_backup_attempted=False, session_integrity='INCONCLUSIVE: flash cannot be identified',
    flash_upload_attempted=False, device_reset=False, bootloader_entered=False,
    gps_backup_command_sent=False, cycles=[], recording_or_erase_commands_sent=False,
    automatic_backup_enabled=False, firmware_modified=False, merged_or_pushed=False,
    serial_released=pre['serial_released'])
(out / 'hardware-result.json').write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
report = root / 'docs/hardware-gps-backup-test-report.md'
text = report.read_text(encoding='utf-8')
text += '''

## Read-only preflight retry after owner reported ready

**BLOCKED — same external flash communication failure persisted.** Previous evidence was preserved; this retry is recorded separately in `logs/hardware-gps-backup-2026-11-03/hardware-run-20261103-retry1/`.

- COM8 again matched VID:PID 2886:8044 and serial 0E8A89B7289430BD. The corrected source, ELF/package identity and all selected artifact checksums still match the approved manifest. Firmware was not rebuilt or modified.
- Recorder idle checks passed again: V1 STOPPED, OBS/fast IMU OFF, COMP idle/no fault/empty queues, TCB state=1/error=none/lock=0.
- Existing firmware GPS: valid checksum NMEA at 38400, no checksum failures, CURRENT RAM readback verified 100 ms / RMC 10 Hz / GGA 1 Hz; NO FIX. These observations still do not test the corrected candidate or GPS standby/wake.
- All five JEDEC reads were `000000`. Status/REMS/SFDP remained all zero and qualification UNRESOLVED. Identical responses reporting repeatability PASS do not establish a working flash device.
- The strengthened preflight stopped at identification. No session-export retry, reset, bootloader entry, firmware upload, GPS backup command, recording/erase command or recovery procedure followed. The serial port was closed. Recording contents and all three GPS cycle outcomes remain INCONCLUSIVE.
- Full raw serial evidence: `before-upload-serial.txt`; structured checks: `preflight-result.json`, `candidate-verification.json`, `hardware-result.json` in the retry directory.

The observed result narrows the blocker to the external SPI flash communication path, not the candidate checksum or GPS UART. Software observations cannot distinguish missing power, wiring/contact problems, a held/disabled flash or a device fault. The expected SPI mapping remains CS D2, SCK D8, XIAO MISO D9 to flash DO, XIAO MOSI D10 to flash DI, with shared ground and the documented module supply. Physical connection/supply verification is required before another upload preflight; no firmware workaround was attempted.
'''
report.write_text(text, encoding='utf-8')
print(json.dumps(result, indent=2))
