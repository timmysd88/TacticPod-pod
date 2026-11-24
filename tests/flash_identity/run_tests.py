"""Production DIAG ID/parser/adapter against traced SPI; no devices."""
import sys,struct
from pathlib import Path
sys.dont_write_bytecode=True
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tcb3'))
from native import Native,compile
elf=compile('tests/flash_identity/harness.cpp','flash-identity-tests',('tests/flash_write',))
names=['full repeated raw capture and transaction framing','BUSY refuses further reads','WEL preserved and refuses further reads',
       'unavailable status','USB backpressure','capture cancelled when OBS starts','request refused during OBS',
       'disconnect cancels','unstable SFDP reported','malformed SFDP never follows pointers','mutation opcode/address allowlist']
for case,name in enumerate(names):
    m=Native(elf);result=m.call('identityCase',case)
    assert result==0,(case,name,result)
    if case==0:
        raw=m.get('Serial',262144);start=raw.index(b'BEGIN_FLASH_ID')
        end=raw.index(b'\n',raw.index(b'END_FLASH_ID',start))+1
        (Path(__file__).resolve().parents[2]/'.pio/phase3b4-identity-example.txt').write_bytes(raw[start:end])
    print('PASS:',name)
print(f'PASS: {len(names)} read-only identity scenarios')
for case,name in enumerate(('faulted recorder with valid flash','Winbond manufacturer reported','busy status blocks reads',
                            'unavailable status reports raw FF','WEL blocks reads','ownership conflict',
                            'factory output under USB backpressure',
                            'all-zero responses do not prove status communication')):
    result=Native(elf).call('factoryCase',case)
    assert result==0,(case,name,result)
    print('PASS: FACTORY DIAG',name)
for case,name in enumerate(('XTX identity and writable status','Winbond SFDP and QE=1',
                            'wrong capacity','wrong erase opcode','unsupported address mode',
                            'unstable SFDP','unresponsive JEDEC','block protection',
                            'complement protection','individual lock mode','unavailable SR3',
                            'suspended operation','reserved SR3 bit is not protection','advertised 256-byte page',
                            'incompatible advertised page')):
    result=Native(elf).call('compatibilityCase',case)
    assert result==0,(case,name,result)
    print('PASS: compatibility',name)
elf=compile('tests/flash_identity/recording.cpp','flash-identity-recording-tests',('tests/recording','tests/flash_write'))
for case,name in enumerate(('V1 CSV export remains silent','V1 recording refuses identity without SPI access',
                            'startup JEDEC fault retained and read-only identity permitted',
                            'startup busy fault retained and blocks identity reads',
                            'Winbond append and CSV readback preserve prior recording')):
    m=Native(elf);result=m.call('identityRecordingCase',case)
    assert result==0,(name,result)
    print('PASS:',name)

from native import ROOT
from tcb3_decode import decode_packets,decode_image
elf=compile('tests/flash_identity/reusable.cpp','flash-compat-reusable',
    ('tests/recording','tests/flash_write','lib/TinyGPSPlus/src'),
    (ROOT/'lib/TinyGPSPlus/src/TinyGPS++.cpp',))
m=Native(elf);m.call('reusableInit')
seed=(ROOT/'tests/reusable/fixtures/physical-three-sessions.bin').read_bytes()
m.m.mem_write(m.s['SPI'],seed);m.call('useWinbond')
result=m.call('reusableCase',9,limit=4000000000)
assert result==0,('Winbond reusable session',result)
info=struct.unpack('<24I',m.get('reusableInfo',96))
assert bytes(m.m.mem_read(m.s['SPI'],len(seed)))==seed
image=bytes(m.m.mem_read(m.s['SPI'],info[0]))
decoded=decode_image(image)
assert not decoded['issues'] and decoded['sessions'][-1]['status']=='complete'
exported,issues=decode_packets(m.get('usbBytes',info[6]))
assert not issues and decode_image(exported)['sessions'][-1]['raw']==decoded['sessions'][-1]['raw']
print('PASS: Winbond reusable append, preserved sessions and TPX3 readback')
