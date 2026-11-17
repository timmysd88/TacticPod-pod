"""Off-device production protocol/ADC integration; never opens a serial device."""
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tcb3'))
from native import Native,compile,ROOT
elf=compile('tests/reusable/gps_power.cpp','gps-power-tests')
names=('normal 38400 wake','9600 RAM-only baud recovery with lost ACK','missing receiver bounded failure',
       'unsupported firmware fails closed','wrong baud readback fails closed','baud NAK fails closed',
       'short TX fails closed','stuck DMA bounded failure','missing readback fails closed',
       'wrong layer/key cannot verify wake','golden PMREQ packet','timer wrap','repeated wake cycles')
for c,name in enumerate(names):
    m=Native(elf);code=m.call('powerCase',c);assert code==0,(name,code)
    print('PASS:',name,flush=True)
elf=compile('tests/reusable/idle_power_integration.cpp','idle-power-integration',
            ('tests/recording','tests/flash_write','lib/TinyGPSPlus/src'),
            (ROOT/'lib/TinyGPSPlus/src/TinyGPS++.cpp',))
seed=(ROOT/'tests/reusable/fixtures/physical-three-sessions.bin').read_bytes()
names=('actual PMREQ, flash sleep and full profile admission','factory baud and lost RAM profile recovery',
       'failed wake blocks capture, explicit retry recovers','baud rejection blocks capture, explicit retry recovers',
       'admission/recording/export/reset/identity owners refuse standby','RAW diagnostic refuses standby','stuck sleep TX rolls back flash sleep',
       'short sleep TX rolls back flash sleep','MCU-only reset wakes a sleeping GPS safely',
       'repeated sleep/wake preserves idle and stored prefix','unidentified firmware refuses PMREQ',
       'UART recovery excludes competing diagnostics',
       'stale one-byte DMA completion cannot mask a stuck wake TX',
       'boot wake recovery respects a latched UART diagnostic fault')
for c,name in enumerate(names):
    m=Native(elf);m.call('reusableInit');m.m.mem_write(m.s['SPI'],seed)
    code=m.call('idlePowerCase',c,limit=4000000000)
    if code:print(m.get('Serial',30000)[8:].split(b'\0',1)[0].decode('utf-8','replace'))
    assert code==0,(name,code)
    assert bytes(m.m.mem_read(m.s['SPI'],len(seed)))==seed
    assert bytes(m.m.mem_read(m.s['SPI']+0xfff000,4096))==b'\xa5'*4096
    print('PASS:',name,flush=True)
