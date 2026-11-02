"""Real main-loop stop/LED/button and GPS standby/reset regressions; no device."""
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tcb3'))
from native import ROOT,Native,compile
elf=compile('tests/reusable/field_integration.cpp','field-integration-tests',
    ('tests/recording','tests/flash_write','lib/TinyGPSPlus/src'),
    (ROOT/'lib/TinyGPSPlus/src/TinyGPS++.cpp',))
seed=(ROOT/'tests/reusable/fixtures/physical-three-sessions.bin').read_bytes()
for c,name in enumerate(('main-loop delayed footer then exactly one green stop event; held/bounced button; idle off',
    'failed finalisation never confirms green', 'receiver RAM reset during standby recovered before one button session',
    'awake profile loss uses ACK and actual readback recovery',
    'GPS recovery commit rejection prevents capture after rollback',
    'missing receiver prevents capture and reports failure',
    'button held through boot never starts',
    'bounded field trace accounts for overwrites and USB backpressure')):
    m=Native(elf);m.call('reusableInit');m.m.mem_write(m.s['SPI'],seed)
    code=m.call('fieldCase',c,limit=4000000000)
    if code:
        print(m.get('Serial',30000)[8:].split(b'\0',1)[0].decode('utf-8','replace'))
    assert code==0,(name,code)
    # The unchanged fixture's committed historical prefix remains intact.
    assert bytes(m.m.mem_read(m.s['SPI'],len(seed)))==seed
    print('PASS:',name,flush=True)
