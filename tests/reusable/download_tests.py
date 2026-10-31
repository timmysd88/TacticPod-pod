"""Read-only host transport with fake streams, never a physical serial port."""
from pathlib import Path
import sys,io,hashlib
ROOT=Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT/'tools'))
from tcb3_download import capture
from verify_tcb3_append import verify
class Link:
    def __init__(self,data):self.data=io.BytesIO(data);self.sent=bytearray()
    def write(self,data):self.sent.extend(data)
    def flush(self):pass
    def read(self,n):return self.data.read(min(n,7))
raw=(ROOT/'.pio/switchless-example.tpx3').read_bytes()
link=Link(raw);dest=io.BytesIO();r=capture(link,dest,10)
assert link.sent==b'TCB READ\n' and dest.getvalue()==raw
assert r['bytes']==len(raw) and r['sha256']==hashlib.sha256(raw).hexdigest()
print('PASS: split raw reads, exact bytes/hash, only existing read-only command')
bad=bytearray(raw);bad[25]^=1
for data,error in ((bad,ValueError),(raw[:10],TimeoutError),(b'FAULT\n'+raw,ValueError)):
    link=Link(data);dest=io.BytesIO()
    try:capture(link,dest,.05)
    except error:pass
    else:raise AssertionError('corrupt/interrupted export accepted')
    assert link.sent==b'TCB READ\n' and dest.getvalue()
print('PASS: bad CRC, interruption and unexpected text preserve partial data; no retry')
before=(ROOT/'tests/reusable/fixtures/physical-three-sessions.bin').read_bytes()
r=verify(before,raw,10);assert r['counts']==[1001,100,10,0] and r['session_id']==58
for a,b,d in ((before,raw,30),(before,before,10),(before,bytes(bad),10)):
    try:verify(a,b,d)
    except ValueError:pass
    else:raise AssertionError('invalid append accepted')
print('PASS: offline append preservation, counts, CRCs, timestamps; invalid duration/missing session/corruption rejected')
