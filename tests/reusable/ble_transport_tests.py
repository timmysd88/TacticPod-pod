"""Exercise the real BLE adapter with mocked notification acceptance and ACKs."""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tcb3'))
from native import Native, compile

elf = compile('tests/reusable/ble_transport.cpp', 'ble-transport-tests',
              ('tests/reusable/ble_transport', 'tests/flash_write'),
              ('--specs=nosys.specs', '-Wall', '-Wextra', '-Werror'))
for name, arguments in (
    ('fastAckCase', (0, 1)),
    ('slowAckCase', (0,)),
    ('backpressureCase', (0, 1)),
    ('retryBoundCase', (0, 1)),
    ('cancelCase', (0, 1)),
    ('disconnectCase', (0,)),
    ('cacheNotReadyCase', (0, 1)),
    ('performanceTransportCase', (0, 1)),
    ('performanceListCase', (0,)),
    ('performanceReadCase', (0,)),
    ('performanceBoundsCase', (0,)),
):
    for argument in arguments:
        result = Native(elf).call(name, argument)
        assert result == 0, (name, argument, 'C++ line', result)
    print('PASS:', name, flush=True)
