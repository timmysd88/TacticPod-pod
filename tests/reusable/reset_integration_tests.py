"""RECORD RESET end to end: real console commands, session, compressor queues and store, with paced sensors.

Host simulation only (Unicorn-emulated firmware build on a NOR/SPI model). It proves logic, ordering, queue
behaviour and loss counters under MODELLED erase times. It does not measure real flash erase latency, real USB
host buffering or real power-cut physics; see docs/fast-record-reset.md for the hardware-only list.
"""
import json,struct,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tcb3'))
from native import ROOT,Native,compile
from tcb3_decode import decode_packets,decode_image
seed=(ROOT/'tests/reusable/fixtures/physical-three-sessions.bin').read_bytes()
elf=compile('tests/reusable/reset_integration.cpp','reusable-reset-integration-tests',
    ('tests/recording','tests/flash_write','lib/TinyGPSPlus/src'),(ROOT/'lib/TinyGPSPlus/src/TinyGPS++.cpp',))
FLASH=0x1000000
def machine(data=None):
    m=Native(elf);m.call('reusableInit')
    m.m.mem_write(m.s['SPI'],seed if data is None else data);return m
def info(m):return struct.unpack('<40I',m.get('resetInfo',160))
def image(m):return bytes(m.m.mem_read(m.s['SPI'],FLASH))
def split(packets):
    at=packets.find(b'TPX3');assert at>=0,'no export packets'
    text=packets[:at];assert text==b'' or text.endswith(b'\n'),'text and binary spliced'
    img,issues=decode_packets(packets[at:]);assert not issues,issues
    return text,packets[at:],decode_image(img)['sessions']
def exported(m,i):return split(m.get('usbBytes',i[22]))
def call(m,name,*a):return m.call(name,*a,limit=4000000000)
report={}

# 1. Paced lazy-erase recording: 100 Hz IMU (noisy = incompressible worst case) and 10 Hz GPS after a real reset.
def paced(erase,seconds,noise,must):
    m=machine()
    for k,v in (('pacedErase',erase),('pacedSeconds',seconds),('pacedNoise',noise)):m.word(k,v)
    code=call(m,'resetPaced');i=info(m)
    lost=[i[12+k]-i[16+k] for k in range(4)]
    row=dict(erase_ms=erase,seconds=seconds,noisy=noise,code=code,locked=i[0],finished=i[1],rejected=i[2],missed=i[3],
             max_service_us=i[4],input_high=i[5],output_high=i[6],max_block_ms=i[7],lazy_erases=i[24],
             max_loop_pass_us=i[20],offered=list(i[12:16]),committed=list(i[16:20]),lost=lost,violations=i[10],
             reset_loop_passes=i[30])
    report['paced_%d_%d'%(erase,noise)]=row
    clean=(code==0 and not i[0] and i[1] and not i[2] and not i[3] and not i[10] and i[11]==0 and not any(lost)
           and any(abs(v-seconds*100)<=seconds*2 for v in row['offered']) and any(abs(v-seconds*10)<=seconds for v in row['offered']))
    if must:
        assert clean,row
        _,_,ss=exported(m,i);assert [s['session_id'] for s in ss]==[2],[s['session_id'] for s in ss]
        s=ss[0];assert s['status']=='complete' and not s['issues'] and s['footer']['lost']==[0]*4 and s['metadata'].get('reset_epoch')==1
        assert i[24]>=(8 if noise else 1),row
        assert i[5]<=2 and i[6]<=2,row
        assert row['max_loop_pass_us']<20000,row # the 400 ms erase is polled, never waited for inside a main-loop pass
    print(('PASS' if must and clean else 'INFO'),'paced',row,flush=True)
    return clean
for erase,noise,must in ((45,1,True),(400,1,True),(400,0,True),(20,1,True)):paced(erase,30,noise,must)
for erase in (1000,2500):paced(erase,30,1,False)   # margin probes: reported, not required

# 2. Serial command sequence through the real console.
m=machine();code=call(m,'resetCommands',1);assert code==0,code
img=image(m);assert img[:4]==b'TPR3' and img[4096:0x39000]==seed[4096:0x39000],'reset must not touch old sectors'
i=info(m);assert i[24]>i[25]>0
_,_,ss=exported(m,(0,)*22+(i[25],));assert ss==[],'old sessions visible after reset'
m=machine();code=call(m,'resetCommands',2);assert code==0,code
i=info(m);_,_,ss=exported(m,i);assert [s['session_id'] for s in ss]==[2] and ss[0]['status']=='complete'
assert not i[0] and i[1] and not i[2] and not i[3]
print('PASS: arming warning, 30 s expiry, busy/export refusals, reset, hidden RECORD READ, empty export, new recording',flush=True)
m=machine();code=call(m,'resetLocked',0);assert code==0,code
after=image(m);m2=machine(after);assert call(m2,'resetCommands',1)==0
print('PASS: latched fault refuses RECORD RESET; it works again after reboot',flush=True)

# 3. Power cut during the real reset command, then reboot and recover.
m=machine();assert call(m,'resetCut',10**7)==0;total=info(m)[30];assert total>1000 and info(m)[31]==0
cuts=sorted({0,1,2,3,5,10,total//2,total-400,total-200,total-120,total-80,total-60,total-40,total-25,total-12,total-6,total-3,total-1,total})
kinds={0:0,1:0,2:0}
for cut in cuts:
    m=machine();assert call(m,'resetCut',cut)==0
    flash=image(m)
    a=machine(flash);assert call(a,'resetRecover',0)==0,cut
    ia=info(a);kind=ia[28];kinds[kind]+=1
    if kind==0:
        assert ia[26] and ia[32]>=0x39000,(cut,ia[26],ia[32])
        _,_,ss=exported(a,ia);ids=[s['session_id'] for s in ss];assert ids[:3]==[1,3,12],(cut,ids)
    elif kind==2:
        assert ia[26] and ia[32]>=0x1000,(cut,ia[26],ia[32])
        _,_,ss=exported(a,ia);assert not {1,3,12}&{s['session_id'] for s in ss},cut
    else:
        assert not ia[26] and ia[27] and ia[29]==0,(cut,ia[26],ia[27],ia[29]) # fail closed, nothing programmed
    b=machine(flash);assert call(b,'resetRecover',1)==0,cut
    ib=info(b);_,_,ss=exported(b,ib);assert [s['session_id'] for s in ss]==[2] and ss[0]['status']=='complete' and not ib[10],(cut,ss)
print('PASS: power cut at',len(cuts),'points of the real reset command; outcomes (old/closed/reset)',kinds,flush=True)
assert kinds[0] and kinds[1] and kinds[2],kinds

# 4. USB: 256-byte FIFO, 31-byte room, random room with console noise, and a partly sent status line before TCB READ.
streams={}
for mode,name in ((0,'fifo256'),(1,'room31'),(2,'random+noise'),(3,'partial-line')):
    m=machine();code=call(m,'usbCase',mode);assert code==0,(mode,code)
    i=info(m);text,binary,ss=exported(m,i)
    assert [s['session_id'] for s in ss]==[1,3,12],(name,[s['session_id'] for s in ss])
    if mode==3:assert text.startswith(b'TCB STATUS') and text.count(b'\n')==1,text
    else:assert text==b'',(name,text[:60])
    streams[name]=binary
assert len(set(streams.values()))==1,'binary export differs with USB room'
print('PASS: TCB READ is byte-identical for 256 B, 31 B, random room with console noise, and after a partly sent status line',flush=True)

# 5. DIAG ID after a latched fault.
for mode,name in ((0,'flash idle'),(1,'flash busy then idle'),(2,'fault line partly sent')):
    m=machine();code=call(m,'diagAfterFault',mode);assert code==0,(name,code)
print('PASS: DIAG ID after a latched fault: idle, busy-then-idle, and no splicing into a partly sent fault line',flush=True)

(ROOT/'.pio/reset-integration.json').write_text(json.dumps(report,indent=2)+'\n')
print('queue/loss report written to .pio/reset-integration.json')
