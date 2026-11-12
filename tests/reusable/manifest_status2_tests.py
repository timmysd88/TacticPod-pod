"""Read-only regressions for BLE reset-epoch scanning; no hardware."""
import struct
import sys
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tcb3'))
from native import ROOT, Native, compile
from tcb3_decode import decode_image, decode_packets

ELF = compile('tests/reusable/manifest.cpp', 'manifest-status2-tests',
              extra=('-DTACTICPOD_REUSABLE=1', '-DTACTICPOD_TCB3_FLASH_ENABLED=1'))
END = 0xfff000
SECTOR = 4096
FLASH_BYTES = 0x1000000
SEED = (ROOT / 'tests/reusable/fixtures/physical-three-sessions.bin').read_bytes()


def marker(epoch):
    page = bytearray(b'\xff' * 256)
    struct.pack_into('<II', page, 0, 0x33525054, epoch)
    struct.pack_into('<I', page, 248, zlib.crc32(page[:248]))
    struct.pack_into('<I', page, 252, 0xc0dec0de)
    return page


def relocated():
    image = bytearray(b'\xff' * END)
    image[:256] = marker(7)
    for offset in range(0, len(SEED), 256):
        page = bytearray(SEED[offset:offset + 256])
        if page == b'\xff' * 256:
            continue
        struct.pack_into('<I', page, 4, struct.unpack_from('<I', page, 4)[0] + 1)
        if offset % SECTOR == 0:
            struct.pack_into('<I', page, 120, 7)
        struct.pack_into('<I', page, 248, zlib.crc32(page[:248]))
        image[SECTOR + offset:SECTOR + offset + 256] = page
    return bytes(image)


def scan(image, status=0, read_failure=False):
    machine = Native(ELF)
    flash = image + b'\xa5' * (FLASH_BYTES - len(image))
    machine.put('nor', flash)
    machine.word('manifestTestStatus', status)
    machine.put('manifestTestReadFailure', bytes([read_failure]))
    assert machine.call('manifestBegin', len(image)) == 0
    for _ in range(200000):
        if machine.call('manifestUpdate', 16):
            break
    else:
        raise AssertionError('scan instruction budget')
    error = None
    if machine.call('manifestFailed'):
        address = machine.call('manifestError')
        error = bytes(machine.m.mem_read(address, 128)).split(b'\0', 1)[0].decode()
    assert machine.get('nor', FLASH_BYTES) == flash
    return machine, error


def usb_export(image):
    machine = Native(ELF)
    flash = image + b'\xa5' * (FLASH_BYTES - len(image))
    machine.put('nor', flash)
    machine.call('exportAllBegin')
    packets = bytearray()
    for _ in range(200000):
        length = machine.call('exportStep')
        if length:
            packets.extend(machine.get('exportPacket', length))
            machine.call('exportRelease')
        if machine.call('exportDone'):
            break
    else:
        raise AssertionError('export instruction budget')
    assert not machine.call('exportFailed')
    assert machine.get('nor', FLASH_BYTES) == flash
    decoded, issues = decode_packets(packets)
    assert not issues
    return decode_image(decoded)


def session_ids(machine):
    ids = []
    for index in range(machine.call('manifestCount')):
        assert machine.call('manifestEntry', index)
        ids.append(struct.unpack_from('<I', machine.get('entryData', 84))[0])
    return ids


image = relocated()
doc = usb_export(image)
assert not doc['issues']
assert [s['session_id'] for s in doc['sessions']] == [2, 4, 13]
assert all(s['status'] == 'complete' and not s['issues'] for s in doc['sessions'])
machine, error = scan(image)
assert error is None, error
assert session_ids(machine) == [2, 4, 13]
header = image[0x39000:0x39100]
assert header[:4] == b'TPH3'
assert struct.unpack_from('<I', header, 20)[0] == 45
print('PASS: USB and BLE discover the same 3 complete current-epoch sessions, '
      'including the multi-sector session spanning 0x39000')

short = bytearray(image)
short[3 * SECTOR:] = b'\xff' * (END - 3 * SECTOR)
doc = usb_export(bytes(short))
assert not doc['issues'] and len(doc['sessions']) == 1
machine, error = scan(bytes(short))
assert error is None and session_ids(machine) == [2]
print('PASS: completed current-epoch session entirely below 0x39000 is discovered')

incomplete_epoch = bytearray(image)
incomplete_epoch[SECTOR + 0xb000 + 45 * SECTOR + 15 * 256:
                 SECTOR + 0xb000 + 45 * SECTOR + 16 * 256] = b'\xff' * 256
machine, error = scan(bytes(incomplete_epoch))
assert error is None and session_ids(machine) == [2, 4]
assert machine.call('manifestIncomplete') == 1
print('PASS: post-reset incomplete final session is excluded and counted safely')

for status, read_failure, expected in (
        (255, False, 'flash status/read failed during manifest marker check'),
        (1, False, 'flash status/read failed during manifest marker check'),
        (0, True, 'flash status/read failed during manifest marker check')):
    _, error = scan(image, status, read_failure)
    assert error == expected
print('PASS: unavailable/busy flash and genuine read refusal have distinct read/status failure reason')

legacy = SEED + b'\xff' * (END - len(SEED))
machine, error = scan(legacy)
assert error is None and machine.call('manifestCount') == 3
print('PASS: original multi-session fixture and partially filled full-size partition scan correctly')

incomplete = bytearray(legacy)
footer = 0xb000 + 45 * SECTOR + 15 * 256
incomplete[footer:footer + 256] = b'\xff' * 256
machine, error = scan(bytes(incomplete))
assert error is None and machine.call('manifestCount') == 2
assert machine.call('manifestIncomplete') == 1
print('PASS: missing final footer is counted as incomplete, not scan failure')

incomplete = bytearray(legacy)
footer = SECTOR + 15 * 256
incomplete[footer:footer + 256] = b'\xff' * 256
machine, error = scan(bytes(incomplete))
assert error == 'TCB3 continuation identity or sequence mismatch'
print('PASS: missing footer followed immediately by another session currently fails continuation parsing')

many = bytearray(b'\xff' * END)
for index in range(65):
    base = index * 2 * SECTOR
    for offset in range(0, 2 * SECTOR, 256):
        page = bytearray(SEED[offset:offset + 256])
        if page == b'\xff' * 256:
            continue
        struct.pack_into('<I', page, 4, base // SECTOR + 1)
        struct.pack_into('<I', page, 248, zlib.crc32(page[:248]))
        many[base + offset:base + offset + 256] = page
machine, error = scan(bytes(many))
assert error is None and machine.call('manifestCount') == 64
assert machine.call('manifestOverflow') == 1
print('PASS: 65 valid sessions produce overflow (LIST status 1), not scanner failure (status 2)')

for argument in sys.argv[1:]:
    path = Path(argument)
    raw = path.read_bytes()
    reconstructed, issues = decode_packets(raw)
    assert not issues
    headers = [struct.unpack_from('<I', reconstructed, at + 120)[0]
               for at in range(0, len(reconstructed), SECTOR)
               if reconstructed[at:at + 4] == b'TPH3']
    assert headers and len(set(headers)) == 1
    image = bytearray(reconstructed)
    image.extend(b'\xff' * (END - len(image)))
    assert image[:256] == b'\xff' * 256
    image[:256] = marker(headers[0])
    # USB omits reset-marker pages. This is a modeled marker, not a raw flash capture.
    machine, error = scan(bytes(image))
    decoded = decode_image(reconstructed)
    assert not decoded['issues']
    expected_ids = [session['session_id'] for session in decoded['sessions']
                    if session['status'] == 'complete']
    print('ARCHIVED EXPORT with modeled epoch marker:', path.name,
          'epoch=', headers[0], 'scanner_error=', error,
          'sessions=', session_ids(machine))
    assert error is None, error
    assert session_ids(machine) == expected_ids
