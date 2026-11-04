"""RECORD RESET: logical reset, reboot recovery, interrupted resets and subsequent recording.

Host simulation only: a Unicorn-emulated build of the real store/reset engine against a NOR model that
rejects programming of already-programmed bits and erases while busy (both counted as violations).
"""
import struct,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tcb3'))
from native import ROOT,Native,compile
from tcb3_decode import decode_image,decode_packets
elf=compile('tests/reusable/reset.cpp','reusable-reset-tests')
SIZE=0x1000000;DIAG=0xfff000
seed=(ROOT/'tests/reusable/fixtures/physical-three-sessions.bin').read_bytes()
base=bytearray(b'\xff'*SIZE);base[:len(seed)]=seed
SENTINEL=bytes((i*7+3)&0xff for i in range(4096));base[DIAG:]=SENTINEL # reserved diagnostic sector
base=bytes(base)
m=Native(elf)

def boot(image=None):
    """Power-on: optionally load a flash image, then reset all counters and the engine."""
    if image is not None:m.m.mem_write(m.s['nor'],image)
    m.call('init',0)
def nor():return bytes(m.m.mem_read(m.s['nor'],SIZE))
def scan():
    for _ in range(20000):
        s=m.call('tick',1)
        if s in (1,8):return s
    raise AssertionError('scan did not finish')
def export():
    out=bytearray();n=m.call('exporting',1)
    while n:
        if n!=0xffffffff:out+=m.get('exportPacket',n)
        n=m.call('exporting')
    # A fail-closed export ends with a fault packet and no END; the host decoder rightly rejects it.
    try:return decode_packets(bytes(out))
    except ValueError as e:return None,[str(e)]
def sessions():
    image,issues=export()
    if image is None:return [],issues
    return [s['session_id'] for s in decode_image(image)['sessions']],issues
def violations():return m.word('violations')
def erases():
    n=m.word('eraseTraceCount');return list(struct.unpack('<'+'I'*n,m.get('eraseTrace',4*n))) if n else []
def reset(limit=100000):
    m.call('resetStart',0);r=m.call('resetTick',limit);return r
def marker(image):
    page=image[:256]
    ok=page[:4]==b'TPR3' and struct.unpack('<I',page[248:252])[0]==__import__('zlib').crc32(page[:248]) \
       and struct.unpack('<I',page[252:256])[0]==0xc0dec0de
    return struct.unpack('<I',page[4:8])[0] if ok else None

# 1. Baseline: the three historical sessions are visible before any reset.
boot(base)
ids,issues=sessions();assert ids==[1,3,12] and not issues,ids
print('PASS: fixture shows sessions',ids)

# 2. Reset: only sector 0 is erased, two programs (revoke + marker), nothing else touched.
boot(base);t0=m.word('nowMs');assert reset()==2
elapsed=m.word('nowMs')-t0;image=nor()
assert erases()==[0] and m.word('programs')==2 and violations()==0
assert marker(image)==1
assert image[4096:DIAG]==base[4096:DIAG],'old sectors must not be erased by RECORD RESET'
assert image[DIAG:]==SENTINEL,'reserved diagnostic sector changed'
print('PASS: reset erased only sector 0, wrote epoch-1 marker, preserved reserved sector; simulated %d ms (1 ms/tick)'%elapsed)

# 3. Reboot recovery: old sessions invisible to export and scan; allocation restarts at sector 1; no write yet.
boot();assert scan()==1 and m.word('info')==1
assert struct.unpack('<2I',m.get('info',8))==(1,0x1000) and m.word('programs')==0 and violations()==0
assert erases()==[0x1000],erases() # the stale head sector is erased lazily, and nothing else
ids,issues=sessions();assert ids==[] and not issues
print('PASS: reboot after reset shows no sessions, next allocation 0x1000, one lazy erase')

# 4. Subsequent recording: reuses low sectors, erases each stale sector first, never overwrites live data.
boot();assert scan()==1
assert m.call('record',10)==0 and violations()==0
e=erases();assert e and all(a>=0x1000 and a%4096==0 and a<DIAG for a in e) and len(e)==len(set(e))
ids,issues=sessions();assert ids==[2] and not issues,ids
image,_=export();s=decode_image(image)['sessions'][0]
assert s['status']=='complete' and s['metadata'].get('reset_epoch')==1 and len(s['raw'])>0
assert nor()[DIAG:]==SENTINEL and max(m.get('pageProgramCounts',65536))==1
print('PASS: new session',ids,'epoch 1, complete,',len(e),'lazy erases; one program per page; reserved sector untouched')

# 5. Second recording continues after the live prefix (and still hides stale sessions beyond it).
boot();assert scan()==1 and m.word('info')==1
head=struct.unpack('<2I',m.get('info',8))[1];assert head>0x1000
assert m.call('record',3)==0 and violations()==0
ids,issues=sessions();assert len(ids)==2 and ids[0]==2 and ids[1]==head//4096+1 and not issues,(ids,head)
print('PASS: later session appended after live data:',ids)

# 6. Second reset: epoch advances, earlier recordings disappear, recording works again.
assert reset()==2 and marker(nor())==2 and violations()==0
boot();assert scan()==1 and sessions()==([],[])
assert m.call('record',2)==0 and sessions()[0]==[2]
image,_=export();assert decode_image(image)['sessions'][0]['metadata']['reset_epoch']==2
print('PASS: repeated reset advances to epoch 2 and hides epoch-1 sessions')

# 7. Epoch never recurs even when the marker is torn: older headers are scanned for the highest epoch.
boot(base);assert reset()==2
boot();assert scan()==1 and m.call('record',4)==0           # epoch-1 session
m.call('resetStart',0);m.word('tearCall',m.word('programs')+2);m.word('tearBytes',8) # tear the marker program
r=m.call('resetTick',100000);assert r in (2,3)
boot();state=scan()
assert state==8 and sessions()[0]==[],'torn marker must fail closed'
boot();assert reset()==2 and marker(nor())>=2,'new epoch must exceed every epoch ever written'
boot();assert scan()==1 and sessions()==([],[])
print('PASS: torn marker fails closed; retry picks an epoch above every previous one')

# 8. Interrupted reset at every step: power cut at each tick around the destructive phase, then reboot.
boot(base);total_ticks=0
m.call('resetStart',0)
while m.call('resetTick',1)==1:total_ticks+=1
assert total_ticks>4000
cuts=list(range(0,8))+list(range(total_ticks-140,total_ticks+3))
outcomes={'old':0,'closed':0,'reset':0}
for cut in cuts:
    boot(base);m.call('resetStart',0);m.call('resetTick',cut)
    boot()                                                    # power cut: engine RAM lost, flash retained
    state=scan();ids,issues=sessions();image=nor()
    if image[:4]==b'TPH3':
        assert state==1 and ids==[1,3,12],(cut,state,ids);outcomes['old']+=1       # reset not yet destructive
    elif marker(image):
        assert state==1 and ids==[] and not issues and m.word('info')==1,(cut,state,ids);outcomes['reset']+=1
    else:
        assert state==8 and ids==[],(cut,state,ids);outcomes['closed']+=1           # revoked/erased/torn: fail closed
    assert violations()==0
    boot();assert reset()==2                                  # every interrupted state recovers by repeating the command
    boot();s=scan();x=sessions();mk=marker(nor());assert s==1 and x==([],[]) and mk>=1,(cut,s,x,mk)
print('PASS: power cut at',len(cuts),'points; outcomes',outcomes)
assert outcomes['old'] and outcomes['closed'] and outcomes['reset']

# 9. Torn programs and erases inside a reset never expose old recordings.
for name,setup in (('torn revoke',lambda b:(m.word('tearCall',1),m.word('tearBytes',b))),
                   ('torn marker',lambda b:(m.word('tearCall',2),m.word('tearBytes',b))),
                   ('torn erase',lambda b:(m.word('eraseTearCall',1),m.word('tearBytes',b)))):
    for tear in (0,1,2,3,4,16,200,255):
        boot(base);setup(tear);m.call('resetStart',0);m.call('resetTick',100000)
        boot();state=scan();ids,issues=sessions()
        image=nor()
        if image[:4]==b'TPH3':assert state==1 and ids==[1,3,12],(name,tear,state,ids)
        else:assert ids==[] ,(name,tear,state,ids)
        assert violations()==0,(name,tear)
        boot();r=reset();assert r==2,(name,tear,r)
print('PASS: torn revoke/marker/erase fail closed or leave old state intact, and are recoverable')

# 10. Interrupted lazy erase while recording: the half-erased head sector is reused after reboot.
boot(base);assert reset()==2
boot();assert scan()==1;m.word('eraseTearCall',3);m.word('tearBytes',512)
rc=m.call('record',10);assert rc in (0,4),rc
boot();state=scan();assert violations()==0
assert state==1 and m.word('info')==1                          # the torn head sector is stale, not live
ids,issues=sessions();assert ids==[2],ids                      # session 2 keeps only its committed sectors
assert m.call('record',2)==0 and violations()==0
ids,issues=sessions();assert len(ids)==2 and ids[0]==2 and not issues,ids
print('PASS: interrupted lazy erase leaves a stale head that is re-erased and reused after reboot')

# 11. A fully erased flash records from the protected 0x39000 boundary; RECORD RESET still works on it.
boot(b'\xff'*SIZE);assert scan()==1 and struct.unpack('<2I',m.get('info',8))[1]==0x39000
assert m.call('record',2)==0 and violations()==0
boot();assert scan()==1 and sessions()[0]==[58]
assert m.call('record',2)==0 and violations()==0                 # repeated recording on fresh flash
boot();assert scan()==1;ids,issues=sessions();assert len(ids)==2 and ids[0]==58 and not issues,ids
assert nor()[:0x39000]==b'\xff'*0x39000                          # nothing written below the boundary
boot(nor());assert reset()==2
boot();assert scan()==1 and sessions()==([],[]) and struct.unpack('<2I',m.get('info',8))==(1,0x1000)
assert m.call('record',2)==0 and sessions()[0]==[2]
print('PASS: fresh flash records at 0x39000 without history, repeats, and quick reset still applies')

# 11b. Seeded legacy flash is unchanged: appends at the same boundary as before.
boot(bytes(base));assert scan()==1 and struct.unpack('<2I',m.get('info',8))[1]==0x39000
print('PASS: legacy seeded flash unchanged')

# 12. Marker or unexpected sector-0 bytes with later data never expose old sessions.
bad=bytearray(base);bad[:4]=b'\0\0\0\0'
boot(bytes(bad));assert scan()==8 and sessions()[0]==[]
bad=bytearray(base);bad[:4096]=b'\xff'*4096
boot(bytes(bad));assert scan()==8 and sessions()[0]==[]
print('PASS: revoked or blank sector 0 with older data above fails closed')

# 13. Torn live-sector header: a matching epoch word alone never makes a stale sector live.
boot(base);assert reset()==2
boot();assert scan()==1;assert m.call('record',3)==0 and violations()==0
boot();assert scan()==1;head=struct.unpack('<2I',m.get('info',8))[1];assert 0x1000<head<0x39000
live=nor();stale=base[head:head+4096];assert stale[:4]==b'TPH3'
def with_head(sector):
    img=bytearray(live);img[head:head+4096]=sector;return bytes(img)
# a. Old header with the epoch bits raised to the current epoch but a bad CRC, old data behind it.
torn=bytearray(stale);torn[120:124]=struct.pack('<I',1)
boot(with_head(bytes(torn)));state=scan();ids,issues=sessions()
assert state==8 and ids==[2] and not issues,(state,ids,issues)
# b. Every single-bit damage of a sealed old header still fails closed rather than becoming live.
for bit in (0,7,8,100,123,247):
    t=bytearray(stale);t[120:124]=struct.pack('<I',1);t[bit//8]^=1<<(bit%8)
    boot(with_head(bytes(t)));state=scan();ids,_=sessions()
    assert state in (1,8) and ids in ([2],[]) and not {1,3,12}&set(ids) and violations()==0,(bit,state,ids)
# c. Unsealed header with blank data behind it is a torn head: reused, never live.
torn=bytearray(b'\xff'*4096);torn[:248]=stale[:248];torn[120:124]=struct.pack('<I',1)
boot(with_head(bytes(torn)));assert scan()==1 and struct.unpack('<2I',m.get('info',8))[1]==head
ids,issues=sessions();assert ids==[2] and not issues,ids
assert m.call('record',2)==0 and violations()==0 and head in erases()
ids,issues=sessions();assert len(ids)==2 and ids[0]==2 and not issues,ids
# d. A sealed header from an older epoch ends the live run cleanly.
boot(with_head(stale));assert scan()==1 and struct.unpack('<2I',m.get('info',8))[1]==head
print('PASS: unsealed or CRC-bad headers never become live (fail closed or reused as torn head)')

# 14. Partly written reset marker: blank start with later bytes programmed never reads as fresh or legacy flash.
for name,data in (('with old data',base),('fresh flash',b'\xff'*SIZE)):
    for off in (16,64,248,252):
        part=bytearray(data);part[:4096]=b'\xff'*4096;part[off:256]=bytes(range(off,256)) if off<248 else b'\x00'*(256-off)
        boot(bytes(part));state=scan();ids,_=sessions()
        assert state==8 and ids==[] and violations()==0,(name,off,state,ids)
        boot();assert m.call('init',0)==0
        boot(bytes(part));assert reset()==2 and marker(nor())>=1 and violations()==0,(name,off)
        boot();assert scan()==1 and sessions()==([],[]),(name,off)
print('PASS: partly written marker fails closed in scan and export, and RECORD RESET recovers it')
