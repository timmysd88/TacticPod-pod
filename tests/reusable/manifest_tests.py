"""Read-only manifest scanning and per-session transport regression tests."""
import struct
import sys
import zlib
from pathlib import Path

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tcb3'))
from native import ROOT,Native,compile
from tcb3_decode import decode_image,decode_packets

ELF=compile('tests/reusable/manifest.cpp','reusable-manifest-tests',
            extra=('-DTACTICPOD_REUSABLE=1','-DTACTICPOD_TCB3_FLASH_ENABLED=1'))
FIXTURE=(ROOT/'tests/reusable/fixtures/physical-three-sessions.bin').read_bytes()
SECTOR=4096

def scan(data,allowFailure=False):
    machine=Native(ELF)
    machine.put('nor',data)
    assert machine.call('manifestBegin',len(data))==0
    for _ in range(200000):
        if machine.call('manifestUpdate',1):break
    else:raise AssertionError('manifest scan did not finish')
    if machine.call('manifestFailed') and not allowFailure:
        address=machine.call('manifestError')
        error=bytearray()
        while (value:=machine.m.mem_read(address+len(error),1)[0])!=0:error.append(value)
        raise AssertionError(error.decode())
    assert machine.get('nor',len(data))==data,'manifest scan modified flash bytes'
    return machine

def entry(machine,index):
    assert machine.call('manifestEntry',index)==1
    return struct.unpack('<21I',machine.get('entryData',84))

machine=scan(FIXTURE)
assert not machine.call('manifestFailed')
assert machine.call('manifestCount')==3
rows=[entry(machine,i) for i in range(3)]
assert [row[0] for row in rows]==[1,3,12]
assert [row[3] for row in rows]==[2,9,46]
assert all(row[5]&3==3 for row in rows)
assert not machine.call('manifestIncomplete')
print('PASS: physical three-session manifest, page/block CRCs, footer counts and identities')

damaged=bytearray(FIXTURE)
damaged[45056+256+32]^=1
machine=scan(damaged,True)
assert machine.call('manifestFailed') and machine.call('manifestCount')==2
print('PASS: corrupt stored page fails closed without exposing the damaged session')

incomplete=bytearray(FIXTURE)
incomplete[45056+45*SECTOR+15*256:45056+45*SECTOR+16*256]=b'\xff'*256
machine=scan(incomplete)
assert not machine.call('manifestFailed')
assert machine.call('manifestIncomplete')==1 and machine.call('manifestCount')==2
print('PASS: missing final footer is reported as an incomplete session')

unknown=bytearray(FIXTURE)
unknown[0:16]=b'UNKNOWN-FORMAT!!'
machine=scan(unknown,True)
assert machine.call('manifestFailed')
print('PASS: unknown occupied sector-zero data fails closed')

reset=bytearray(FIXTURE)
marker=bytearray(b'\xff'*256)
struct.pack_into('<II',marker,0,0x33525054,7)
struct.pack_into('<I',marker,248,zlib.crc32(marker[:248]))
struct.pack_into('<I',marker,252,0xc0dec0de)
reset[:256]=marker
machine=scan(reset)
assert not machine.call('manifestFailed') and machine.call('manifestCount')==0
print('PASS: reset epoch marker hides stale pre-reset sessions')

writeStart=0x39000
resetged=bytearray(b'\xff'*(writeStart+2*SECTOR))
resetged[:256]=marker
resetged[2*SECTOR:11*SECTOR]=FIXTURE[2*SECTOR:11*SECTOR]
current=bytearray(FIXTURE[:2*SECTOR])
for offset in range(0,len(current),256):
    page=bytearray(current[offset:offset+256])
    if page==b'\xff'*256:continue
    struct.pack_into('<I',page,4,writeStart//SECTOR+1)
    if offset%SECTOR==0:struct.pack_into('<I',page,120,7)
    struct.pack_into('<I',page,248,zlib.crc32(page[:248]))
    current[offset:offset+256]=page
resetged[writeStart:writeStart+len(current)]=current
machine=scan(resetged,True)
assert not machine.call('manifestFailed') and machine.call('manifestCount')==1,(
    machine.call('manifestFailed'),machine.call('manifestCount'),machine.call('manifestIncomplete'))
row=entry(machine,0)
assert row[0]==58 and row[1]==7 and row[2]==writeStart and row[3]==2
print('PASS: post-reset epoch sessions are discovered beyond stale pre-reset storage')

def export_session():
    machine=Native(ELF)
    machine.put('nor',FIXTURE)
    assert machine.call('exportBegin',3)==1
    packets=bytearray()
    for _ in range(20000):
        length=machine.call('exportStep')
        if length:
            packets.extend(machine.get('exportPacket',length))
            machine.call('exportRelease')
        if machine.call('exportDone'):break
    else:raise AssertionError('session export did not finish')
    assert not machine.call('exportFailed')
    assert machine.get('nor',len(FIXTURE))==FIXTURE,'session export modified flash bytes'
    return bytes(packets)

packetStream=export_session()
assert export_session()==packetStream,'restarted session export changed the stream'
decoded,issues=decode_packets(packetStream)
assert not issues
image=decode_image(decoded)
assert [session['session_id'] for session in image['sessions']]==[3]
assert image['sessions'][0]['status']=='complete'
print('PASS: targeted export emits only the selected session in existing TPX3 transport')

frames=[];cursor=0
while cursor<len(packetStream):
    length=struct.unpack_from('<H',packetStream,cursor+16)[0]+24
    frames.append(packetStream[cursor:cursor+length]);cursor+=length
missing=next(index for index,frame in enumerate(frames) if frame[5]==2)
try:decode_packets(b''.join(frames[:missing]+frames[missing+1:]))
except ValueError:pass
else:raise AssertionError('export decoder accepted a missing transport packet')
damaged=bytearray(packetStream);damaged[frames[1].__len__()+32]^=1
try:decode_packets(bytes(damaged))
except ValueError:pass
else:raise AssertionError('export decoder accepted a corrupted transport packet')
print('PASS: missing/corrupt packets are rejected; disconnect-style restart is deterministic and read-only')
