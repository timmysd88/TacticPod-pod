"""BLE lifecycle/session integration with modeled NOR; no radio or hardware access."""
from host_checks import run_cpp
from pathlib import Path
import sys

run_cpp('tests/reusable/ble_phase1.cpp', 'ble-phase1-tests')
print('PASS: lazy BLE initialization, interlocks, all reset paths, inactivity and post-reset button')
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tcb3'))
from native import ROOT, Native, compile
from tcb3_decode import decode_image, decode_packets

elf = compile('tests/reusable/ble_field_integration.cpp', 'ble-field-integration',
              ('tests/recording', 'tests/flash_write', 'lib/TinyGPSPlus/src'),
              (ROOT / 'lib/TinyGPSPlus/src/TinyGPS++.cpp',))
seed = (ROOT / 'tests/reusable/fixtures/physical-three-sessions.bin').read_bytes()
recorded = decode_image(seed)['sessions'][0]
for case in range(13):
    machine = Native(elf)
    machine.call('reusableInit')
    machine.m.mem_write(machine.s['SPI'], seed)
    assert machine.call('bulkReadCase', case) == 0, ('bulk read adapter', case)
print('PASS: USB/BLE 256-byte DMA transaction, RAM zeros, CS, alignment/end bounds and ownership guards')
for case, name in enumerate(('unchanged recording hold/start/stop', 'advertising timeout',
                             'advertising hold cancellation', 'connected hold and inactivity',
                             'connected disconnection', 'double-tap from low-power standby',
                             'cached LIST leaves flash asleep and GET exports read-only',
                             'failed flash wake refuses GET', 'wrong JEDEC refuses GET',
                             'busy flash after wake refuses GET', 'cache scan failure diagnostic',
                             'recorder ownership blocks LIST', 'pending recorder blocks LIST',
                             'open page blocks LIST without touching SPI', 'recorder ownership blocks GET',
                             'LIST cancellation preserves sleeping flash', 'GET disconnection sleeps flash',
                             'GET cancellation sleeps flash', 'repeated GET wakes and sleeps flash')):
    machine = Native(elf)
    machine.call('reusableInit')
    machine.m.mem_write(machine.s['SPI'], seed)
    result = machine.call('bleFieldCase', case, limit=4000000000)
    if result:
        print(machine.get('Serial', 30000)[8:].split(b'\0', 1)[0].decode('utf-8', 'replace'))
    assert result == 0, (name, result)
    if case in (6, 18):
        image, issues = decode_packets(machine.get('usbBytes', machine.word('usbSize')))
        assert not issues
        sessions = decode_image(image)['sessions']
        assert len(sessions) == 1 and not sessions[0]['issues']
        assert sessions[0]['raw'] == recorded['raw']
        assert sessions[0]['footer'] == recorded['footer']
    expected = bytearray(seed)
    if case == 10:
        expected[0] = ord('?')  # Deliberately injected malformed manifest input.
    assert bytes(machine.m.mem_read(machine.s['SPI'], len(seed))) == expected
    if case >= 6:
        expected.extend(b'\xff' * (0xfff000 - len(expected)))
        expected.extend(b'\xa5' * 4096)
        assert bytes(machine.m.mem_read(machine.s['SPI'], len(expected))) == expected
    print('PASS: real main loop with mock BLE adapter:', name, flush=True)
