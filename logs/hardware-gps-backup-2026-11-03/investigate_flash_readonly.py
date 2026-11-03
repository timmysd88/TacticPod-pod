"""Compare the existing legacy/raw read-only diagnostics. No wake/reset/upload."""
from pathlib import Path
import importlib.util
import json
import re

root = Path(__file__).resolve().parents[2]
out = Path(__file__).with_name('flash-detection-investigation')
out.mkdir(exist_ok=False)
spec = importlib.util.spec_from_file_location('hardware_monitor', Path(__file__).with_name('hardware_run.py'))
hw = importlib.util.module_from_spec(spec)
spec.loader.exec_module(hw)
monitor = hw.Monitor(out / 'readonly-serial.txt')
result = dict(commands=[], firmware_modified=False, uploaded=False, reset=False,
              flash_wake_command_sent=False, flash_array_or_status_writes=False)
try:
    result['initial'] = monitor.read(2)
    assert 'TPX3' not in result['initial']
    result['states'] = monitor.idle()
    result['commands'] += ['RECORD STATUS', 'OBS STATUS', 'COMP STATUS', 'TCB STATUS']
    result['battery'] = monitor.command('BAT STATUS', 'BAT source=')
    result['commands'].append('BAT STATUS')
    # DIAG is the legacy 9F/5A-only reader; verified in source before use.
    monitor.allowed = monitor.allowed | {'DIAG'}
    result['legacy_diag'] = monitor.command('DIAG', 'SFDP summary covers', 15)
    result['commands'].append('DIAG')
    result['raw_diag'] = monitor.command('DIAG ID', 'END_FLASH_ID', 15)
    result['commands'].append('DIAG ID')
    result['raw_jedec'] = re.findall(r'FLASH ID JEDEC \d+ ([0-9A-F]+)', result['raw_diag'])
    result['qualification_is_not_a_hardware_test'] = True
    print('Legacy JEDEC lines:', '\n'.join(l for l in result['legacy_diag'].splitlines() if 'FLASH JEDEC' in l))
    print('Raw JEDEC:', result['raw_jedec'])
finally:
    monitor.close()
    result['serial_released'] = True
    (out / 'readonly-result.json').write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
