"""Validated RAM manifest readiness, invalidation and ownership; no hardware."""
from pathlib import Path
import struct
import sys
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tcb3'))
from native import ROOT, Native, compile

elf = compile('tests/reusable/ble_cache.cpp', 'ble-cache-tests',
              ('tests/recording', 'tests/flash_write', 'lib/TinyGPSPlus/src'),
              (ROOT / 'lib/TinyGPSPlus/src/TinyGPS++.cpp',))
seed = (ROOT / 'tests/reusable/fixtures/physical-three-sessions.bin').read_bytes()

def run(name, data=seed, expected=(0, 3, 0, 0)):
    machine = Native(elf)
    machine.call('reusableInit')
    machine.m.mem_write(machine.s['SPI'], data)
    machine.put('cacheExpected', struct.pack('<4I', *expected))
    result = machine.call(name, limit=4000000000)
    assert result == 0, (name, expected, 'C++ line', result)
    if name not in ('cacheAppendCase', 'cacheResetCase', 'cacheEraseGuardCase'):
        assert bytes(machine.m.mem_read(machine.s['SPI'], len(data))) == data
    print('PASS:', name, expected, flush=True)

for case in ('cacheReadyCase', 'cacheContentionCase', 'cacheStandbyCase',
             'cacheAppendCase', 'cacheResetCase', 'cacheEraseGuardCase', 'cachePerformanceCase'):
    run(case)

damaged = bytearray(seed)
damaged[45056 + 256 + 32] ^= 1
run('cacheValidationCase', bytes(damaged), (1, 0, 0, 0))

incomplete = bytearray(seed)
footer = 45056 + 45 * 4096 + 15 * 256
incomplete[footer:footer + 256] = b'\xff' * 256
run('cacheValidationCase', bytes(incomplete), (0, 2, 1, 0))

many = bytearray(b'\xff' * (65 * 2 * 4096))
for index in range(65):
    base = index * 2 * 4096
    for offset in range(0, 2 * 4096, 256):
        page = bytearray(seed[offset:offset + 256])
        if page == b'\xff' * 256:
            continue
        struct.pack_into('<I', page, 4, base // 4096 + 1)
        struct.pack_into('<I', page, 248, zlib.crc32(page[:248]))
        many[base + offset:base + offset + 256] = page
run('cacheValidationCase', bytes(many), (0, 64, 0, 1))
