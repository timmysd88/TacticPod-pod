"""Production DIAG ID/parser/adapter against traced SPI; no devices."""
import sys
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
elf=compile('tests/flash_identity/recording.cpp','flash-identity-recording-tests',('tests/recording','tests/flash_write'))
for case,name in enumerate(('V1 CSV export remains silent','V1 recording refuses identity without SPI access')):
    m=Native(elf);result=m.call('identityRecordingCase',case)
    assert result==0,(name,result)
    print('PASS:',name)
