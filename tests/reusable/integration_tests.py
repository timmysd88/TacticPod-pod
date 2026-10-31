import sys,json,struct,hashlib
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tcb3'))
from native import ROOT,Native,compile
from tcb3_decode import decode_packets,decode_image
seed=(ROOT/'tests/reusable/fixtures/physical-three-sessions.bin').read_bytes()
assert len(seed)==0x39000 and hashlib.sha256(seed).hexdigest()=='b9ad38d83ac27a61a79bf58cbbba68cc44a03d6dddeebcf287d9166eb76fb6f6'
elf=compile('tests/reusable/integration.cpp','reusable-integration-tests',
    ('tests/recording','tests/flash_write','lib/TinyGPSPlus/src'),(ROOT/'lib/TinyGPSPlus/src/TinyGPS++.cpp',))
def machine(data=seed):
    m=Native(elf);m.call('reusableInit');m.m.mem_write(m.s['SPI'],data);return m
results=[]
for c,name in enumerate(('USB-only three consecutive bounded sessions','calibrated low drain and finalisation',
    'calibrated hard threshold finalisation','uncalibrated cell start refuses','hidden occupied initial sector faults',
    'insufficient capacity refuses before programming','power interruption and append recovery',
    'finalisation watchdog without flash slack','TIMER4 rollover','USB-only low phantom ignored','acquisition fault finalises and locks',
    'USB battery status while recording and export isolation','power interruption after a committed block')):
    m=machine();code=m.call('reusableCase',c,limit=4000000000);assert code==0,(c,name,code)
    info=struct.unpack('<24I',m.get('reusableInfo',96))
    assert bytes(m.m.mem_read(m.s['SPI'],len(seed)))==seed
    if c in (0,1,2,8,9,11):
        image=bytes(m.m.mem_read(m.s['SPI'],info[0]));doc=decode_image(image);assert not doc['issues']
        assert [s['session_id'] for s in doc['sessions'][:3]]==[1,3,12]
        fresh=doc['sessions'][3:];assert len(fresh)==(3 if c==0 else 1)
        assert all(s['status']=='complete' and not s['issues'] and s['footer']['lost']==[0]*4 for s in fresh)
        raw=b''.join(s['raw'] for s in fresh);non_events=bytearray();at=0
        while at<len(raw):
            n=struct.unpack_from('<H',raw,at+2)[0]
            if raw[at]!=4:non_events+=raw[at:at+n]
            at+=n
        assert non_events==m.get('newWire',info[2])
        packets=m.get('usbBytes',info[6]);exported,issues=decode_packets(packets);assert not issues
        assert decode_image(exported)['sessions'][-1]['raw']==fresh[-1]['raw']
        for s in doc['sessions']:
            isolated=bytearray(b'\xff'*len(image))
            for a in s['sectors']:isolated[a:a+4096]=image[a:a+4096]
            d=decode_image(bytes(isolated));assert not d['issues'] and len(d['sessions'])==1 and d['sessions'][0]['raw']==s['raw']
        if c in (1,2):
            events=[r for r in fresh[0]['records'] if r['type_id']==4]
            assert any(r['code']==0x100 and r['argument']==(2 if c==1 else 3) for r in events)
            end=next(r for r in events if r['argument']==(2 if c==1 else 3))
            assert ((end['detail']>>32)<=3500 if c==1 else (end['detail']&0xffffffff)<=3300)
        if c==0:
            (ROOT/'.pio/reusable-example.tpx3').write_bytes(packets)
            (ROOT/'.pio/reusable-example.v2wire').write_bytes(b''.join(s['raw'] for s in doc['sessions']))
    if c in (6,12):
        interrupted=bytes(m.m.mem_read(m.s['SPI'],info[0]));assert interrupted[:len(seed)]==seed
        restarted=machine(interrupted);assert restarted.call('reusableCase',9,limit=4000000000)==0
        after=struct.unpack('<24I',restarted.get('reusableInfo',96));assert after[5]>58
        assert bytes(restarted.m.mem_read(restarted.s['SPI'],len(interrupted)))==interrupted
        doc=decode_image(bytes(restarted.m.mem_read(restarted.s['SPI'],after[0])))
        assert doc['issues'] and doc['sessions'][-1]['status']=='complete'
        if c==12:assert any(s['session_id']==58 and s['frames'] for s in doc['sessions'])
    results.append({'case':name,'metrics':info});print('PASS:',name,info,flush=True)
(ROOT/'.pio/reusable-integration.json').write_text(json.dumps(results,indent=2)+'\n')
