"""Portable Phase 1 adapter/reset integration; no radio or flash operations."""
from host_checks import run_cpp
from pathlib import Path
import sys

run_cpp('tests/reusable/ble_phase1.cpp', 'ble-phase1-tests')
print('PASS: lazy BLE initialization, interlocks, all reset paths, inactivity and post-reset button')
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tcb3'))
from native import ROOT, Native, compile

elf = compile('tests/reusable/ble_field_integration.cpp', 'ble-field-integration',
              ('tests/recording', 'tests/flash_write', 'lib/TinyGPSPlus/src'),
              (ROOT / 'lib/TinyGPSPlus/src/TinyGPS++.cpp',))
seed = (ROOT / 'tests/reusable/fixtures/physical-three-sessions.bin').read_bytes()
for case, name in enumerate(('unchanged recording hold/start/stop', 'advertising timeout',
                             'advertising hold cancellation', 'connected hold and inactivity',
                             'connected disconnection', 'double-tap from low-power standby')):
    machine = Native(elf)
    machine.call('reusableInit')
    machine.m.mem_write(machine.s['SPI'], seed)
    result = machine.call('bleFieldCase', case, limit=4000000000)
    if result:
        print(machine.get('Serial', 30000)[8:].split(b'\0', 1)[0].decode('utf-8', 'replace'))
    assert result == 0, (name, result)
    assert bytes(machine.m.mem_read(machine.s['SPI'], len(seed))) == seed
    print('PASS: real main loop with mock BLE adapter:', name, flush=True)
