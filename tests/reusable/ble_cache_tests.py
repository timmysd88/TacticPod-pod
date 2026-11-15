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

def provisional(scenario, data=seed, mutated=False):
    machine = Native(elf)
    machine.call('reusableInit')
    machine.m.mem_write(machine.s['SPI'], data)
    result = machine.call('provisionalCacheCase', scenario, limit=4000000000)
    assert result == 0, (scenario, 'C++ line', result)
    if not mutated:
        assert bytes(machine.m.mem_read(machine.s['SPI'], len(data))) == data
    print('PASS: provisional cache scenario', scenario, flush=True)

provisional(0)
damaged = bytearray(seed)
damaged[45056 + 256 + 32] ^= 1
provisional(1, bytes(damaged))
provisional(2, bytes(incomplete))
reset = bytearray(b'\xff' * (len(seed) + 4096))
marker = bytearray(b'\xff' * 256)
struct.pack_into('<II', marker, 0, 0x33525054, 7)
struct.pack_into('<II', marker, 248, zlib.crc32(marker[:248]), 0xc0dec0de)
reset[:256] = marker
for offset in range(0, len(seed), 256):
    page = bytearray(seed[offset:offset + 256])
    if page == b'\xff' * 256:
        continue
    struct.pack_into('<I', page, 4, struct.unpack_from('<I', page, 4)[0] + 1)
    if offset % 4096 == 0:
        struct.pack_into('<I', page, 120, 7)
    struct.pack_into('<I', page, 248, zlib.crc32(page[:248]))
    reset[4096 + offset:4096 + offset + 256] = page
provisional(3, bytes(reset), mutated=True)
provisional(4)
provisional(5)
provisional(6, bytes(many))
damaged = bytearray(seed)
damaged[45056 + 24] ^= 1
provisional(7, bytes(damaged))
damaged = bytearray(seed)
damaged[:16] = b'UNKNOWN-FORMAT!!'
provisional(8, bytes(damaged))
provisional(9)
provisional(10, mutated=True)
provisional(11)
provisional(12)
provisional(13, mutated=True)
provisional(14, mutated=True)
damaged = bytearray(seed)
at = 45056 + 256
damaged[at + 32] ^= 1
struct.pack_into('<I', damaged, at + 248, zlib.crc32(damaged[at:at + 248]))
provisional(15, bytes(damaged))
damaged = bytearray(seed)
for offset in (32, 64):
    value = struct.unpack_from('<Q', damaged, footer + offset)[0]
    struct.pack_into('<Q', damaged, footer + offset, value + 1)
struct.pack_into('<I', damaged, footer + 248, zlib.crc32(damaged[footer:footer + 248]))
provisional(16, bytes(damaged))
provisional(17, mutated=True)
provisional(18)
