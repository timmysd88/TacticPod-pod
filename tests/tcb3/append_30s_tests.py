"""Append-profile production ARM code with the real Phase 3B.7 seed; no devices."""
import sys,json,struct,hashlib
sys.dont_write_bytecode=True
from native import ROOT,Native,compile
from tcb3_decode import decode_image,decode_packets
seed=(ROOT/'tests/tcb3/fixtures/phase3b7-session.bin').read_bytes()
assert len(seed)==8192 and hashlib.sha256(seed).hexdigest()=='8bf910507bac630695a28b2b7c42a9a34d0d06ebda9a30016cc82d0a898dc1bc'
old=decode_image(seed)['sessions'][0]
elf=compile('tests/tcb3/append_30s.cpp','tcb3-append-30s-tests',
    ('tests/recording','tests/flash_write','lib/TinyGPSPlus/src'),(ROOT/'lib/TinyGPSPlus/src/TinyGPS++.cpp',))
def machine(image=seed):
    m=Native(elf);m.call('appendInit');m.m.mem_write(m.s['SPI'],image);return m
def run(m,case):
    code=m.call('appendScenario',case,limit=4000000000);assert code==0,(case,code)
    return struct.unpack('<20I',m.get('appendInfo',80))
def verify(m,info,expected_start,root_issues=False):
    image=bytes(m.m.mem_read(m.s['SPI'],info[0]));assert image[:8192]==seed
    doc=decode_image(image);assert bool(doc['issues'])==root_issues,doc['issues']
    assert doc['sessions'][0]['raw']==old['raw'] and doc['sessions'][0]['status']=='complete'
    s=doc['sessions'][-1];assert s['status']=='complete' and not s['issues']
    assert s['sectors'][0]==expected_start and s['session_id']==expected_start//4096+1
    assert s['footer']['committed'][0] in (3000,3001) and s['footer']['committed'][1:]==[300,30,0]
    assert s['footer']['lost']==[0]*4 and s['raw']==m.get('appendWire',info[3])
    start=info[13]+(info[14]<<32)
    assert all(start<=r['timestamp_us']<start+30000000 for r in s['records'])
    packets=m.get('usbBytes',info[12]);exported,issues=decode_packets(packets);assert not issues
    copy=decode_image(exported);assert copy['sessions'][0]['raw']==old['raw'] and copy['sessions'][-1]['raw']==s['raw']
    # Each complete session also decodes when other sessions are omitted.
    for session in (doc['sessions'][0],s):
        isolated=bytearray(b'\xff'*len(image))
        for a in session['sectors']:isolated[a:a+4096]=image[a:a+4096]
        independent=decode_image(bytes(isolated));assert len(independent['sessions'])==1
        assert not independent['issues']
        assert independent['sessions'][0]['raw']==session['raw']
    return packets
results=[]
for case,name in enumerate(('normal disconnected 30-second append','hidden initial payload aborts before WREN',
                            'full fence refuses before writes','missing preserved prefix refuses',
                            'torn header preserved; reset refuses another append','interrupted data preserved; reset refuses another append','post-probe mutation refuses',
                            '60-second finalisation watchdog without slack','capture through TIMER4 rollover',
                            'denied WEL faults without overwriting prefix','queue pressure stops with explicit loss',
                            'occupied sector beyond fence refuses before writes',
                            'corrupt initial prefix refuses','partial initial sector last byte refuses',
                            'corrupt predecessor header refuses','occupied continuation sector faults',
                            'repeated START pending active and stopped refuses','first-program identity mismatch refuses',
                            'valid occupied header at unexpected append address refuses','inconsistent preserved metadata refuses')):
    m=machine();info=run(m,case)
    if case in (0,8,16):
        packets=verify(m,info,0x2000)
        if case==8:
            start=info[13]+(info[14]<<32);assert start<2**32<start+30000000
        if case==0:
            (ROOT/'.pio/phase3b9-example.tpx3').write_bytes(packets)
            (ROOT/'.pio/phase3b9-example.v2wire').write_bytes(old['raw']+m.get('appendWire',info[3]))
    elif case in (4,5):
        interrupted=bytes(m.m.mem_read(m.s['SPI'],max(8192,info[0])))
        assert interrupted[:8192]==seed
        damaged=decode_image(interrupted);assert damaged['sessions'][0]['raw']==old['raw']
        if case==5:
            assert damaged['sessions'][-1]['status']=='interrupted' and len(damaged['sessions'][-1]['frames'])>=1
        recovered=machine(interrupted)
        assert recovered.call('appendScenario',0,limit=4000000000)==4
        refused=struct.unpack('<20I',recovered.get('appendInfo',80))
        assert refused[15:18]==(0,0,1),refused
        assert bytes(recovered.m.mem_read(recovered.s['SPI'],len(interrupted)))==interrupted
    elif case==10:
        doc=decode_image(bytes(m.m.mem_read(m.s['SPI'],info[0])));s=doc['sessions'][-1]
        assert doc['sessions'][0]['raw']==old['raw'] and s['status']=='closed_with_loss'
        assert s['footer']['reason']==2 and sum(s['footer']['lost'])>0
    assert bytes(m.m.mem_read(m.s['SPI'],8192))==seed or case in (3,14,19)
    results.append({'case':name,'metrics':info});print('PASS:',name,info,flush=True)
(ROOT/'.pio/phase3b9-append.json').write_text(json.dumps(results,indent=2)+'\n')
elf=compile('tests/tcb3/append_30s_stress.cpp','tcb3-append-stress-tests',
    ('tests/recording','tests/flash_write','lib/TinyGPSPlus/src'),(ROOT/'lib/TinyGPSPlus/src/TinyGPS++.cpp',))
m=machine();info=run(m,0);verify(m,info,0x2000)
print('PASS: 30-second append with 100us per compression unit and 10ms program BUSY',info,flush=True)
(ROOT/'.pio/phase3b9-stress.json').write_text(json.dumps({'metrics':info,'compression_unit_us':100,'program_busy_ms':10})+'\n')
elf=compile('tests/tcb3/append_store.cpp','tcb3-append-store-tests')
m=Native(elf);m.call('init',1);m.m.mem_write(m.s['nor'],seed);m.call('tick',4101)
assert m.word('info')==9 and m.call('cancel')==1 and m.call('start')==0 and m.word('programs')==0
print('PASS: cancelling the read-only append probe cannot allocate from a partial scan',flush=True)
m=Native(elf);m.call('init',1);m.m.mem_write(m.s['nor'],seed);m.call('tick',4101+64)
assert m.word('info')==1
code=m.call('sustain',3,limit=4000000000);assert code==0,code
metrics=struct.unpack('<16I',m.get('sustainedInfo',64));assert metrics[8]==0x40000 and metrics[9]==1 and metrics[7]==0
assert metrics[2]==1 and metrics[3]<2000
assert bytes(m.m.mem_read(m.s['nor'],8192))==seed
assert bytes(m.m.mem_read(m.s['nor']+0x40000,0x1000000-0x40000))==b'\xff'*(0x1000000-0x40000)
programs=m.word('programs');addresses=struct.unpack('<'+'I'*programs,m.get('traceAddress',4*programs))
assert all(0x2000<=a<=0x3ff00 for a in addresses) and max(addresses)==0x3ff00
assert max(m.get('pageProgramCounts',65536))==1
print('PASS: maximum raw-frame pacing to exact append fence, one program per page, old session preserved',metrics,flush=True)
(ROOT/'.pio/phase3b9-fence.json').write_text(json.dumps({'metrics':metrics,'programs':programs,'highest_program_page':max(addresses)})+'\n')

for mode,label in enumerate(('start address tamper','pre-program address tamper','unreadable allocated sector')):
    m=Native(elf);m.call('init',1);m.m.mem_write(m.s['nor'],seed);m.call('tick',4101+64)
    assert m.word('info')==1
    assert m.call('appendGuard',mode)==8,label
    assert m.word('programs')==0 and m.word('violations')==0,label
    assert bytes(m.m.mem_read(m.s['nor'],8192))==seed
    print('PASS: fail closed without any program:',label,flush=True)
for phase in (0,4101):
    m=Native(elf);m.call('init',1);m.m.mem_write(m.s['nor'],seed);m.call('tick',phase)
    m.word('denyRead',1);assert m.call('tick',1)==8 and m.word('programs')==0
print('PASS: unreadable recovery scan/probe fail closed before programming',flush=True)
for phase in (0,1):
    m=Native(elf);m.call('init',1);m.m.mem_write(m.s['nor'],seed)
    assert m.call('appendStatusCase',phase)==8 and m.word('programs')==0
print('PASS: unexpected protection/status bits in scan/probe fault before programming',flush=True)
