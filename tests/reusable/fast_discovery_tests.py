"""Offline metadata-only discovery against the existing full validator."""
import hashlib
import struct
import sys
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tcb3'))
from native import ROOT, Native, compile
from tcb3_decode import decode_packets

ELF = compile('tests/reusable/fast_discovery_harness.cpp', 'fast-discovery-tests',
              extra=('-DTACTICPOD_REUSABLE=1', '-DTACTICPOD_TCB3_FLASH_ENABLED=1'))
END, SECTOR, PAGE, FLASH = 0xfff000, 4096, 256, 0x1000000
SEED = (ROOT / 'tests/reusable/fixtures/physical-three-sessions.bin').read_bytes()
SCANNING, COMPLETE, INCOMPLETE, CORRUPT, AMBIGUOUS, OVERFLOW, IO_ERROR = range(7)


def seal(page):
    struct.pack_into('<I', page, 248, zlib.crc32(page[:248]))
    struct.pack_into('<I', page, 252, 0xc0dec0de)


def marker(epoch):
    page = bytearray(b'\xff' * PAGE)
    struct.pack_into('<II', page, 0, 0x33525054, epoch)
    seal(page)
    return page


def flash_image(data):
    assert len(data) <= END
    return bytes(data) + b'\xff' * (FLASH - len(data))


def scan(image, fast=True, status=0, read_failure=False):
    m = Native(ELF)
    m.put('nor', image)
    m.word('manifestTestStatus', status)
    m.put('manifestTestReadFailure', bytes([read_failure]))
    assert not m.call('scanBegin', int(fast))
    for _ in range(5000):
        if m.call('scanFast' if fast else 'scanFull', 128):
            break
    else:
        raise AssertionError('scan exceeded its bounded step budget')
    assert m.get('nor', FLASH) == image, 'read-only scan changed flash'
    return m


def fast_entries(m):
    rows = []
    for i in range(m.call('fastCount')):
        assert m.call('fastEntry', i) == 1, 'candidate must be explicitly provisional'
        row = struct.unpack('<29I', m.get('candidateData', 116))
        assert row[5] == 0, 'provisional candidate must not claim validation'
        rows.append(row)
    return rows


def full_entries(m):
    rows = []
    for i in range(m.call('manifestCount')):
        assert m.call('manifestEntry', i)
        rows.append(struct.unpack('<21I', m.get('entryData', 84)))
    return rows


def compare(image, label):
    fast, full = scan(image), scan(image, False)
    assert fast.call('fastResult') == COMPLETE and not full.call('manifestFailed')
    a, b = fast_entries(fast), full_entries(full)
    assert len(a) == len(b)
    for provisional, validated in zip(a, b):
        assert provisional[:5] == validated[:5]
        assert provisional[6:13] == validated[6:13]
        assert provisional[13:21:2] == validated[13:17]
        assert provisional[21:29:2] == validated[17:21]
        assert all(provisional[i] == 0 for i in (14, 16, 18, 20, 22, 24, 26, 28))
        assert validated[5] & 3 == 3
    print(f'{label}: ids={[r[0] for r in a]} ranges={[(r[2], r[3]) for r in a]}')
    print(f'  provisional steps={fast.word("scanSteps")} reads={fast.word("readCalls")} bytes={fast.word("readBytes")}')
    print(f'  full        steps={full.word("scanSteps")} reads={full.word("readCalls")} bytes={full.word("readBytes")}')
    assert fast.word('scanSteps') < full.word('scanSteps')
    assert fast.word('readBytes') < full.word('readBytes')


compare(flash_image(SEED), 'existing three-session fixture')

damaged = bytearray(SEED)
damaged[0xb000 + PAGE + 32] ^= 1
m = scan(flash_image(damaged))
assert m.call('fastResult') == COMPLETE and m.call('fastCount') == 3
assert scan(flash_image(damaged), False).call('manifestFailed')
print('PASS: data corruption remains provisional and full validation rejects it')

damaged = bytearray(SEED)
damaged[0xb000 + 24] ^= 1
m = scan(flash_image(damaged))
assert m.call('fastResult') == CORRUPT and m.call('fastCount') == 0

damaged = bytearray(SEED)
footer = 0xb000 + 45 * SECTOR + 15 * PAGE
damaged[footer + 32] ^= 1
assert scan(flash_image(damaged)).call('fastResult') == CORRUPT
page = bytearray(SEED[footer:footer + PAGE])
struct.pack_into('<Q', page, 32, 0)
seal(page)
damaged[footer:footer + PAGE] = page
assert scan(flash_image(damaged)).call('fastResult') == CORRUPT
print('PASS: header CRC, footer CRC and footer accounting fail closed')

damaged = bytearray(SEED)
damaged[footer:footer + PAGE] = b'\xff' * PAGE
m = scan(flash_image(damaged))
assert m.call('fastResult') == INCOMPLETE and m.call('fastIncomplete') == 1 and m.call('fastCount') == 2

damaged = bytearray(SEED)
first_footer = SECTOR + 15 * PAGE
damaged[first_footer:first_footer + PAGE] = b'\xff' * PAGE
m = scan(flash_image(damaged))
assert m.call('fastResult') == INCOMPLETE and m.call('fastIncomplete') == 1
assert [row[0] for row in fast_entries(m)] == [3, 12]

damaged = bytearray(SEED)
continuation = 0xc000
page = bytearray(damaged[continuation:continuation + PAGE])
struct.pack_into('<I', page, 20, 42)
seal(page)
damaged[continuation:continuation + PAGE] = page
assert scan(flash_image(damaged)).call('fastResult') == CORRUPT
damaged = bytearray(SEED)
page = bytearray(damaged[continuation:continuation + PAGE])
struct.pack_into('<I', page, 8, 15)
seal(page)
damaged[continuation:continuation + PAGE] = page
assert scan(flash_image(damaged)).call('fastResult') == CORRUPT

damaged = bytearray(SEED)
damaged[:16] = b'UNKNOWN-FORMAT!!'
assert scan(flash_image(damaged)).call('fastResult') == AMBIGUOUS
damaged = bytearray(b'\xff' * SECTOR)
damaged[20] = 0
assert scan(flash_image(damaged)).call('fastResult') == AMBIGUOUS
print('PASS: incomplete session, invalid continuation and ambiguous markers are distinguished')

reset = bytearray(b'\xff' * (len(SEED) + SECTOR))
reset[:PAGE] = marker(7)
for offset in range(0, len(SEED), PAGE):
    page = bytearray(SEED[offset:offset + PAGE])
    if page == b'\xff' * PAGE:
        continue
    struct.pack_into('<I', page, 4, struct.unpack_from('<I', page, 4)[0] + 1)
    if offset % SECTOR == 0:
        struct.pack_into('<I', page, 120, 7)
    seal(page)
    reset[SECTOR + offset:SECTOR + offset + PAGE] = page
compare(flash_image(reset), 'modeled epoch fixture')
stale = bytearray(reset)
page = bytearray(stale[3 * SECTOR:3 * SECTOR + PAGE])
struct.pack_into('<I', page, 120, 6)
seal(page)
stale[3 * SECTOR:3 * SECTOR + PAGE] = page
m = scan(flash_image(stale))
assert m.call('fastResult') == COMPLETE and [r[0] for r in fast_entries(m)] == [2]
damaged = bytearray(reset)
damaged[248] ^= 1
assert scan(flash_image(damaged)).call('fastResult') == CORRUPT
damaged = bytearray(reset)
damaged[3 * SECTOR + 120] ^= 1
assert scan(flash_image(damaged)).call('fastResult') == CORRUPT
print('PASS: sealed different-epoch tail is excluded')

wide = bytearray(SEED)
page = bytearray(wide[footer:footer + PAGE])
struct.pack_into('<Q', page, 32, (1 << 32) + 10)
struct.pack_into('<Q', page, 64, (1 << 32) + 10)
seal(page)
wide[footer:footer + PAGE] = page
rows = fast_entries(scan(flash_image(wide)))
assert rows[2][13:15] == (10, 1)
assert scan(flash_image(wide), False).call('manifestFailed')
print('PASS: provisional 64-bit counters are preserved without claiming reconstructed counts')

many = bytearray(b'\xff' * (130 * SECTOR))
for i in range(65):
    base = i * 2 * SECTOR
    for offset in range(0, 2 * SECTOR, PAGE):
        page = bytearray(SEED[offset:offset + PAGE])
        if page == b'\xff' * PAGE:
            continue
        struct.pack_into('<I', page, 4, base // SECTOR + 1)
        seal(page)
        many[base + offset:base + offset + PAGE] = page
m = scan(flash_image(many))
assert m.call('fastResult') == OVERFLOW and m.call('fastCount') == 0 and m.call('fastOverflow')
assert scan(flash_image(SEED), status=1).call('fastResult') == IO_ERROR
assert scan(flash_image(SEED), read_failure=True).call('fastResult') == IO_ERROR
print('PASS: overflow, busy flash and read failure are explicit')

last = bytearray(b'\xff' * END)
base = END - 2 * SECTOR
for offset in range(0, 2 * SECTOR, PAGE):
    page = bytearray(SEED[offset:offset + PAGE])
    if page == b'\xff' * PAGE:
        continue
    struct.pack_into('<I', page, 4, base // SECTOR + 1)
    seal(page)
    last[base + offset:base + offset + PAGE] = page
m = scan(flash_image(last))
rows = fast_entries(m)
assert m.call('fastResult') == COMPLETE and len(rows) == 1 and rows[0][2:4] == (base, 2)
print('PASS: final recording sector is discovered without reading the diagnostic sector')

for argument in sys.argv[1:]:
    path = Path(argument)
    raw = path.read_bytes()
    image, issues = decode_packets(raw)
    assert not issues
    epochs = {struct.unpack_from('<I', image, at + 120)[0]
              for at in range(0, len(image), SECTOR) if image[at:at + 4] == b'TPH3'}
    assert len(epochs) == 1 and image[:PAGE] == b'\xff' * PAGE
    modeled = bytearray(image)
    modeled[:PAGE] = marker(epochs.pop())
    print(f'USB backup SHA-256={hashlib.sha256(raw).hexdigest()}; reset marker modeled, not exported')
    compare(flash_image(modeled), 'saved recording backup')
