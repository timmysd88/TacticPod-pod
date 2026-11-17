"""Combined real main-loop GPS standby, BLE export and fast pacing; no device."""
from pathlib import Path
import sys
sys.dont_write_bytecode=True
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tcb3'))
from native import Native,compile,ROOT
elf=compile('tests/reusable/idle_throughput_integration.cpp','idle-throughput-integration',
    ('tests/recording','tests/flash_write','lib/TinyGPSPlus/src'),
    (ROOT/'lib/TinyGPSPlus/src/TinyGPS++.cpp',))
seed=(ROOT/'tests/reusable/fixtures/physical-three-sessions.bin').read_bytes()
for c,name in enumerate(('BLE startup and GET keep GPS asleep',
    'repeated GET/cancellation never wake GPS','1ms download/10ms idle scheduling with ADC wrapper')):
    m=Native(elf);m.call('reusableInit');m.m.mem_write(m.s['SPI'],seed)
    result=m.call('combinedPowerThroughputCase',c,limit=4000000000)
    assert result==0,(name,result)
    assert bytes(m.m.mem_read(m.s['SPI'],len(seed)))==seed
    assert bytes(m.m.mem_read(m.s['SPI']+0xfff000,4096))==b'\xa5'*4096
    print('PASS:',name,flush=True)
