"""Confirm identity/idle using status-only commands; no raw SPI/wake interface exists."""
from pathlib import Path
import importlib.util
import json

root = Path(__file__).resolve().parents[2]
out = Path(__file__).with_name('flash-wake-capability-check')
out.mkdir(exist_ok=False)
spec = importlib.util.spec_from_file_location('hw', Path(__file__).with_name('hardware_run.py'))
hw = importlib.util.module_from_spec(spec)
spec.loader.exec_module(hw)
device = hw.identify()
result = dict(device=dict(port=device.device, vid=device.vid, pid=device.pid, serial=device.serial_number),
    spi_mapping=dict(CS='D2/P0.28', SCK='D8/P1.13', MISO='D9/P1.14', MOSI='D10/P1.15'),
    spi_settings='1 MHz, mode 0, MSB first; unchanged',
    safe_console_flash_wake_command=False, wake_attempted=False, spi_AB_sent=False,
    reset=False, uploaded=False, recording_contents_modified=False,
    outcome='BLOCKED: existing firmware exposes no safe explicit flash wake command',
    response_after_wake=None, communication_restored='NOT TESTED: wake unavailable',
    source_evidence=['src/serial_console.cpp handlers: DIAG and DIAG ID are read-only',
                     'src/main.cpp: FlashPower::release called only from setup and leaveStandby',
                     'src/main.cpp identityRead allowlist: 05 35 9F 90 5A; AB excluded'])
monitor = hw.Monitor(out / 'status-only-serial.txt')
try:
    result['initial'] = monitor.read(2)
    assert 'TPX3' not in result['initial'], 'Export active; send no commands'
    result['idle_states'] = monitor.idle()
    result['recording_and_exports_inactive'] = True
finally:
    monitor.close()
    result['serial_released'] = True
    (out / 'capability-result.json').write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
print(json.dumps(result, indent=2))
