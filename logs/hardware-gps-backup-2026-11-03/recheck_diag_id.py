"""One requested DIAG ID at 115200; no wake, reset, upload or array writes."""
from pathlib import Path
from datetime import datetime, timezone
import importlib.util
import json
import re

root = Path(__file__).resolve().parents[2]
out = Path(__file__).with_name('diag-id-recheck-' + datetime.now(timezone.utc).strftime('%Y%m%d-%H%M%S'))
out.mkdir(exist_ok=False)
spec = importlib.util.spec_from_file_location('hw', Path(__file__).with_name('hardware_run.py'))
hw = importlib.util.module_from_spec(spec)
spec.loader.exec_module(hw)
device = hw.identify()
result = dict(port=device.device, baud=115200, serial=device.serial_number,
              requested_command='DIAG ID', vscode_monitor_ui_opened=False,
              reset=False, uploaded=False, wake_sent=False, recording_or_erase_commands_sent=False)
monitor = hw.Monitor(out / 'serial-transcript.txt')
try:
    result['initial'] = monitor.read(2)
    assert 'TPX3' not in result['initial'], 'Export active; command not sent'
    result['idle_states'] = monitor.idle()
    result['settle_output'] = monitor.read(8)
    result['response'] = monitor.command('DIAG ID', 'END_FLASH_ID', 20)
    result['jedec_reads'] = re.findall(r'FLASH ID JEDEC \d+ ([0-9A-F]+)', result['response'])
    try:
        result['communication_validation'] = hw.validate_flash_identity(result['response'],
            (root / 'logs/phase3b4-hardware/sfdp-capture-1.bin').read_bytes())
        result['outcome'] = 'PASS'
    except ValueError as exc:
        result['communication_validation'] = dict(passed=False, reason=str(exc))
        result['outcome'] = 'FAIL'
    print('Port:', device.device, 'baud: 115200')
    print('JEDEC reads:', result['jedec_reads'])
    print('Validation:', result['communication_validation'])
    print('Full transcript:', out / 'serial-transcript.txt')
finally:
    monitor.close()
    result['serial_released'] = True
    (out / 'result.json').write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
