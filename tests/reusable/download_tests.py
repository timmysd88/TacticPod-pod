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
refusal=b'TCB REFUSED: busy/fault state=1 session=0 lock=0 tcb_pending=0 gps_profile_audit_active=1\n'
for data,error in ((bad,ValueError),(raw[:10],TimeoutError),(b'FAULT\n'+raw,ValueError),(refusal,ValueError)):
    link=Link(data);dest=io.BytesIO()
    try:capture(link,dest,.05)
    except error as exc:
        if data==refusal:assert refusal.decode().strip() in str(exc)
    else:raise AssertionError('corrupt/interrupted export accepted')
    assert link.sent==b'TCB READ\n' and dest.getvalue()
if dest.getvalue()!=refusal:raise AssertionError('complete refusal response was not retained')
print('PASS: bad CRC, interruption and unexpected text preserve partial data; no retry')
before=(ROOT/'tests/reusable/fixtures/physical-three-sessions.bin').read_bytes()
r=verify(before,raw,10)
# Fresh GPS admission shifts the acquisition phase; either edge may contain a
# boundary sample. Retain the verifier's duration/rate/gap/CRC/footer checks.
assert abs(r['counts'][0]-1000)<=2 and r['counts'][1:]==[100,10,0] and r['session_id']==58
for a,b,d in ((before,raw,30),(before,before,10),(before,bytes(bad),10)):
    try:verify(a,b,d)
    except ValueError:pass
    else:raise AssertionError('invalid append accepted')
print('PASS: offline append preservation, counts, CRCs, timestamps; invalid duration/missing session/corruption rejected')
