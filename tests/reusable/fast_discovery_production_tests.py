"""Compare the firmware scanner with the preserved Phase 4A prototype."""
import runpy
import struct
import sys
from pathlib import Path

phase4a = runpy.run_path(str(Path(__file__).with_name('fast_discovery_tests.py')))
Native, compile = phase4a['Native'], phase4a['compile']
ELF = compile('tests/reusable/fast_discovery_production_harness.cpp', 'fast-discovery-production-tests',
              extra=('-DTACTICPOD_REUSABLE=1', '-DTACTICPOD_TCB3_FLASH_ENABLED=1',
                     str(phase4a['ROOT'] / 'src/tcb3_fast_discovery.cpp')))
END, SECTOR, PAGE, FLASH = (phase4a[k] for k in ('END', 'SECTOR', 'PAGE', 'FLASH'))
SEED = phase4a['SEED']
image = phase4a['flash_image']
seal = phase4a['seal']


def run(m, function):
    for _ in range(5000):
        if m.call(function, 128):
            return
    raise AssertionError('scan exceeded its bounded step budget')


def rows(m, count_name, entry_name, data_name):
    result = []
    for i in range(m.call(count_name)):
        assert m.call(entry_name, i) == 1
        row = struct.unpack('<29I', m.get(data_name, 116))
        assert row[5] == 0
        result.append(row)
    return result


def parity(data, label, status=0, read_failure=False):
    m = Native(ELF)
    m.put('nor', data)
    m.word('manifestTestStatus', status)
    m.put('manifestTestReadFailure', bytes([read_failure]))
    assert m.call('productionResult') == 7
    m.call('productionPause', 3)
    assert m.word('readCalls') == 0 and m.word('exportStatusPolls') == 0

    assert not m.call('scanBegin', 1)
    run(m, 'scanFast')
    expected = (m.call('fastResult'), m.call('fastIncomplete'), m.call('fastOverflow'),
                rows(m, 'fastCount', 'fastEntry', 'candidateData'),
                m.word('scanSteps'), m.word('readCalls'), m.word('readBytes'))

    assert not m.call('productionBegin', END)
    assert m.call('productionResult') == 0 and m.call('productionCount') == 0
    m.call('productionPause', 10)
    assert m.word('readCalls') == 0 and m.word('exportStatusPolls') == 0
    m.call('productionStep', 4)
    counters = (m.word('readCalls'), m.word('readBytes'), m.word('exportStatusPolls'))
    m.call('productionPause', 10)
    assert counters == (m.word('readCalls'), m.word('readBytes'), m.word('exportStatusPolls'))
    run(m, 'productionStep')
    actual = (m.call('productionResult'), m.call('productionIncomplete'), m.call('productionOverflow'),
              rows(m, 'productionCount', 'productionEntry', 'productionData'),
              m.word('scanSteps'), m.word('readCalls'), m.word('readBytes'))
    assert actual == expected, label
    assert m.word('maxReadsPerStep') <= 1 and m.word('maxStatusPerStep') <= 1
    assert m.word('writeCalls') == 0 and m.get('nor', FLASH) == data
    before = (m.word('readCalls'), m.word('exportStatusPolls'))
    m.call('productionStep', 5)
    assert before == (m.word('readCalls'), m.word('exportStatusPolls'))
    assert m.call('productionBegin', 0) and m.call('productionResult') == phase4a['AMBIGUOUS']
    assert m.call('productionBegin', 1) and m.call('productionResult') == phase4a['AMBIGUOUS']
    assert m.call('productionBegin', END + SECTOR) and m.call('productionResult') == phase4a['AMBIGUOUS']
    assert m.call('productionMissingPort', 1) == phase4a['AMBIGUOUS']
    assert m.call('productionMissingPort', 2) == phase4a['AMBIGUOUS']
    assert m.call('productionCount') == 0
    assert not m.call('productionBegin', END) and m.call('productionCount') == 0
    print(f'PASS: {label}: result={actual[0]}, candidates={len(actual[3])}, '
          f'steps={actual[4]}, read_bytes={actual[6]}')
    return m.call('productionSize')


size = parity(image(SEED), 'original fixture')
parity(image(phase4a['reset']), 'current epoch and reset marker')
parity(image(phase4a['stale']), 'different-epoch tail')
parity(image(phase4a['wide']), '64-bit provisional counters')
parity(image(phase4a['many']), 'manifest overflow')
parity(image(phase4a['last']), 'last usable sectors')
parity(image(SEED), 'busy flash', status=1)
parity(image(SEED), 'read failure', read_failure=True)

damaged = bytearray(SEED)
damaged[0xb000 + 24] ^= 1
parity(image(damaged), 'corrupt header')
damaged = bytearray(SEED)
damaged[phase4a['footer'] + 32] ^= 1
parity(image(damaged), 'corrupt footer')
damaged = bytearray(SEED)
page = bytearray(damaged[phase4a['footer']:phase4a['footer'] + PAGE])
struct.pack_into('<Q', page, 32, 0)
seal(page)
damaged[phase4a['footer']:phase4a['footer'] + PAGE] = page
parity(image(damaged), 'invalid footer accounting')
damaged = bytearray(SEED)
damaged[phase4a['footer']:phase4a['footer'] + PAGE] = b'\xff' * PAGE
parity(image(damaged), 'incomplete recording')
damaged = bytearray(SEED)
first_footer = SECTOR + 15 * PAGE
damaged[first_footer:first_footer + PAGE] = b'\xff' * PAGE
parity(image(damaged), 'incomplete recording followed by a new session')
damaged = bytearray(phase4a['reset'])
damaged[248] ^= 1
parity(image(damaged), 'corrupt reset marker')
damaged = bytearray(SEED)
page = bytearray(damaged[0xc000:0xc000 + PAGE])
struct.pack_into('<I', page, 20, 42)
seal(page)
damaged[0xc000:0xc000 + PAGE] = page
parity(image(damaged), 'invalid sector continuity')
damaged = bytearray(SEED)
damaged[0xb000 + PAGE + 32] ^= 1
parity(image(damaged), 'unvalidated corrupted data page')
damaged = bytearray(SEED)
damaged[:16] = b'UNKNOWN-FORMAT!!'
parity(image(damaged), 'ambiguous metadata')

for argument in sys.argv[1:]:
    raw = Path(argument).read_bytes()
    reconstructed, issues = phase4a['decode_packets'](raw)
    assert not issues
    epochs = {struct.unpack_from('<I', reconstructed, at + 120)[0]
              for at in range(0, len(reconstructed), SECTOR)
              if reconstructed[at:at + 4] == b'TPH3'}
    assert len(epochs) == 1 and reconstructed[:PAGE] == b'\xff' * PAGE
    modeled = bytearray(reconstructed)
    modeled[:PAGE] = phase4a['marker'](epochs.pop())
    parity(image(modeled), 'saved recording export with modeled epoch marker')

print(f'Firmware scanner sizeof={size} bytes (ARM ABI).')
