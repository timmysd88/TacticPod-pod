"""Bounded physical-test build, production ARM main loop and simulated SPI only."""
import sys,json,struct
sys.dont_write_bytecode=True
from native import ROOT,Native,compile
from tcb3_decode import decode_image,decode_packets
elf=compile('tests/tcb3/first_write.cpp','tcb3-first-write-tests',
    ('tests/recording','tests/flash_write','lib/TinyGPSPlus/src'),(ROOT/'lib/TinyGPSPlus/src/TinyGPS++.cpp',))
results=[]
names=('disconnected five-second capture and TPX3 export','unexpected SR2 refuses start',
       'occupied final test sector refuses start','occupied sector beyond fence refuses start',
       'OBS active refuses start','no-slack finalisation deadline faults','missed acquisition stops',
       'held-CS deadline abort faults and never retries','capture deadline through TIMER4 low-word rollover')
for case,name in enumerate(names):
    m=Native(elf);code=m.call('firstWrite',case);assert code==0,(name,code)
    info=struct.unpack('<16I',m.get('firstInfo',64))
    if case in (0,6,8):
        image=bytes(m.m.mem_read(m.s['SPI'],65536));decoded=decode_image(image);s=decoded['sessions'][0]
        assert not decoded['issues'] and not s['issues']
        if case in (0,8):
            assert s['status']=='complete' and s['footer']['lost']==[0]*4
            assert s['raw']==m.get('firstWire',info[6])
            assert s['footer']['committed'][0] in (500,501) and s['footer']['committed'][1:]==[50,5,0],s['footer']
            started=info[12]+(info[13]<<32)
            assert all(started<=r['timestamp_us']<started+5000000 for r in s['records'])
            if case==8:assert s['records'][0]['timestamp_us']<2**32<s['records'][-1]['timestamp_us']
            packets=m.get('usbBytes',info[11]);exported,issues=decode_packets(packets)
            assert not issues and decode_image(exported)['sessions'][0]['raw']==s['raw']
            if case==0:
                (ROOT/'.pio/phase3b6-example.tpx3').write_bytes(packets)
                (ROOT/'.pio/phase3b6-example.v2wire').write_bytes(s['raw'])
        else:assert s['footer']['reason']==3 and s['footer']['missed']==1
    results.append(dict(scenario=name,metrics=info));print('PASS:',name,info,flush=True)
(ROOT/'.pio/phase3b6-first-write.json').write_text(json.dumps(results,indent=2)+'\n')
elf=compile('tests/tcb3/first_store.cpp','tcb3-first-fence-tests')
m=Native(elf);result=m.call('firstFence');assert result==0,result
print('PASS: production engine fills all 16 allowed sectors, reports full, finalises within fence; all later bytes unchanged')
elf=compile('tests/tcb3/first_console.cpp','tcb3-first-console-tests',('tests/recording','tests/flash_write'))
m=Native(elf);result=m.call('firstConsole');assert result==0,result
print('PASS: actual USB console and diagnostic entry refuse all erases/V1 writes; 20-second disconnected boot never records')
