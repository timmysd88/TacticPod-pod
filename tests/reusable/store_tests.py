import sys,struct
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tcb3'))
from native import ROOT,Native,compile
elf=compile('tests/reusable/store.cpp','reusable-store-tests')
seed=(ROOT/'tests/reusable/fixtures/physical-three-sessions.bin').read_bytes()
for mode in (0,1):
    m=Native(elf);m.call('init',1);m.m.mem_write(m.s['nor'],seed);m.call('tick',4095+64)
    assert m.word('info')==1 and m.call('tamper',mode)==8 and m.word('programs')==0
print('PASS: recovered-start and first-program identity tampering fault before programming')
m=Native(elf);m.call('init',1);m.m.mem_write(m.s['nor'],seed)
m.m.mem_write(m.s['nor']+0xffe000,b'\x00');m.call('tick',4096)
assert m.word('info')==8 and struct.unpack('<I',m.get('info',24)[20:24])[0]==1 and m.word('programs')==0
print('PASS: zero free sectors reports full and faults before programming')
m=Native(elf);m.call('init',1);m.m.mem_write(m.s['nor'],seed)
m.m.mem_write(m.s['nor']+0xffd000,b'\x00');m.call('tick',4095+64)
assert m.word('info')==1 and m.call('sustain',3,limit=4000000000)==0
info=struct.unpack('<16I',m.get('sustainedInfo',64));assert info[8]==0xfff000 and info[9]==1 and info[7]==0
assert bytes(m.m.mem_read(m.s['nor'],len(seed)))==seed
assert bytes(m.m.mem_read(m.s['nor']+0xfff000,4096))==b'\xff'*4096
count=m.word('programs');addresses=struct.unpack('<'+'I'*count,m.get('traceAddress',4*count))
assert count==16 and min(addresses)==0xffe000 and max(addresses)==0xffef00
assert max(m.get('pageProgramCounts',65536))==1
print('PASS: last recording sector fills/finalises with explicit loss, 16 single-page programs, diagnostic sector untouched',info)
