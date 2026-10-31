import sys
sys.dont_write_bytecode=True
from native import ROOT,Native,compile
import json,struct
from tcb3_decode import decode_image,decode_packets
from unicorn import UC_HOOK_MEM_WRITE
elf=compile('tests/tcb3/integration.cpp','tcb3-integration-tests',
    ('tests/recording','tests/flash_write','lib/TinyGPSPlus/src'),
    (ROOT/'lib/TinyGPSPlus/src/TinyGPS++.cpp',))
results=[]
for case in range(7):
    m=Native(elf);lowest=[0x23fff000]
    def stack(uc,access,address,size,value,_):lowest[0]=min(lowest[0],address)
    hook=m.m.hook_add(UC_HOOK_MEM_WRITE,stack,begin=0x23ff0000,end=0x23ffffff)
    code=m.call('integration',case);m.m.hook_del(hook)
    metrics=struct.unpack('<24I',m.get('integrationInfo',96))
    print('CASE',case,'result',code,'metrics',metrics,flush=True)
    if code:
        print(m.get('Serial',12000).split(b'\0',1)[0]);raise AssertionError((case,code))
    spi=m.s['SPI'];image=bytes(m.m.mem_read(spi,65536))
    decoded=decode_image(image);session=decoded['sessions'][0]
    if case in (0,4):
        raw=m.get('expectedWire',metrics[15]);rows=[];at=0
        while at<len(raw):
            size=struct.unpack_from('<H',raw,at+2)[0];rows.append(raw[at:at+size]);at+=size
        # Observer diagnostic queues drain by type, so recover producer order:
        # completion-time GPS is consumed before acquisition in main.cpp.
        rows.sort(key=lambda r:(struct.unpack_from('<Q',r,4)[0],0 if r[0]!=1 else 1))
        assert session['raw']==b''.join(rows)
        assert not session['issues'] and session['status']=='complete'
    if case in (1,2):assert session['footer']['reason'] in (2,3)
    if case==3:
        packets=m.get('usbBytes',metrics[16]);exported,issues=decode_packets(packets)
        assert not issues
        assert decode_image(exported)['sessions'][0]['raw']==session['raw']
    if case==5:
        assert len(decoded['sessions'])==2 and len(decoded['legacy']['sessions'])==1
    if case==6:
        assert session['footer']['committed']==[1500,150,15,0] and session['footer']['lost']==[0,0,0,0]
    results.append(dict(case=case,metrics=metrics,footer=session['footer'],observed_harness_stack_bytes=0x23fff000-lowest[0]))
(ROOT/'.pio/phase3b-integration-results.json').write_text(json.dumps(results,indent=2)+'\n')
print('PASS: seven enabled production main-loop/SPI/compressor/session scenarios')
