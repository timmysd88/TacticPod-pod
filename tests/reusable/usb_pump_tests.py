"""Bounded USB scheduling with a draining 256-byte FIFO; not a USB speed benchmark."""
import struct
import sys
from pathlib import Path

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tcb3'))
from native import ROOT,Native,compile
from tcb3_decode import decode_packets,decode_image

elf=compile('tests/reusable/reset_integration.cpp','reusable-usb-pump-tests',
    ('tests/recording','tests/flash_write','lib/TinyGPSPlus/src'),
    (ROOT/'lib/TinyGPSPlus/src/TinyGPS++.cpp',))
seed=(ROOT/'tests/reusable/fixtures/physical-three-sessions.bin').read_bytes()
streams=[]
passes={}
for mode,name in enumerate(('Phase 2 schedule','bounded pump','short/zero writes and FIFO stalls',
                            'disconnect before write and restart','disconnect during write and restart',
                            'read permission withheld','Phase 2 schedule, fast drain','bounded pump, fast drain')):
    machine=Native(elf)
    machine.call('reusableInit')
    machine.m.mem_write(machine.s['SPI'],seed)
    code=machine.call('usbPumpCase',mode,limit=4000000000)
    assert code==0,(name,code)
    assert bytes(machine.m.mem_read(machine.s['SPI'],len(seed)))==seed
    assert bytes(machine.m.mem_read(machine.s['SPI']+0xfff000,4096))==b'\xa5'*4096
    if mode==5:
        print('PASS:',name)
        continue
    info=struct.unpack('<8I',machine.get('usbPumpInfo',32))
    data=machine.get('usbBytes',info[1])
    image,issues=decode_packets(data)
    assert not issues
    doc=decode_image(image)
    assert not doc['issues'] and [s['session_id'] for s in doc['sessions']]==[1,3,12]
    assert all(s['footer'] and not s['issues'] for s in doc['sessions'])
    streams.append(data)
    passes[mode]=info[0]
    assert info[2]<=1 and info[3]<=2
    if mode==2:assert info[4]>0 and info[5]>0
    print('PASS:',name,'passes=',info[0],'max reads/writes=',info[2:4],flush=True)

assert len(set(streams))==1,'USB schedule/backpressure/disconnect changed TPX3 bytes'
for old,new,drain in ((0,1,64),(6,7,256)):
    assert passes[new]<passes[old],('pump did not reduce modeled scheduling passes',drain,passes)
    print('PASS: byte-identical exports; %d B/pass drain, modeled passes reduced by %.1f%% '
          '(not measured hardware throughput)'%(drain,(1-passes[new]/passes[old])*100))
