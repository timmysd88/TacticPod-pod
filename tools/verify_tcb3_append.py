"""Offline validation of one bounded append against a pre-test binary export."""
from pathlib import Path
import argparse,json,hashlib
try:
    from .tcb3_decode import decode_packets,decode_image
except ImportError:
    from tcb3_decode import decode_packets,decode_image

def unpack(data):
    if data[:4]==b'TPX3':
        data,issues=decode_packets(data)
        if issues:raise ValueError('export ISSUE packets')
    doc=decode_image(data)
    if doc['issues']:raise ValueError('container/page issues: '+repr(doc['issues']))
    return data,doc

def verify(before,after,duration):
    old_image,old=unpack(before);image,doc=unpack(after)
    if image[:len(old_image)]!=old_image:raise ValueError('previous exported flash prefix changed')
    previous=old['sessions'];sessions=doc['sessions']
    if len(sessions)!=len(previous)+1 or sessions[:-1]!=previous:raise ValueError('expected one new session, previous sessions unchanged')
    s=sessions[-1];f=s['footer']
    if s['status']!='complete' or s['issues'] or f is None:raise ValueError('new session not complete/clean')
    if f['reason'] or any(f['lost']) or any(f[k] for k in ('rejected','missed','crc_failures','incomplete_writes','full')):
        raise ValueError('new session contains losses or fault counters')
    elapsed=f['stop_us']-s['metadata']['start_us']
    if not duration*1000000<=elapsed<=duration*1000000+50000:raise ValueError('capture duration mismatch')
    counts=f['committed'];expected=(duration*100,duration*10,duration)
    if any(abs(a-b)>t for a,b,t in zip(counts[:3],expected,(2,2,1))):raise ValueError('IMU/RMC/GGA count outside window tolerance')
    times=[r['timestamp_us'] for r in s['records']]
    if times!=sorted(times) or times[0]<s['metadata']['start_us'] or times[-1]>f['stop_us']:
        raise ValueError('timestamp range/order mismatch')
    spacing={}
    for kind in (1,2,3):
        t=[r['timestamp_us'] for r in s['records'] if r['type_id']==kind]
        gaps=[b-a for a,b in zip(t,t[1:])]
        spacing[kind]=dict(count=len(t),min_us=min(gaps) if gaps else None,max_us=max(gaps) if gaps else None,
            observed_hz=(len(t)-1)*1000000/(t[-1]-t[0]) if len(t)>1 and t[-1]>t[0] else None)
        if kind==1 and (not gaps or min(gaps)<=0 or max(gaps)>=20000):raise ValueError('IMU acquisition gap')
    return dict(session_id=s['session_id'],counts=counts,elapsed_us=elapsed,
        sectors=[hex(a) for a in s['sectors']],allocated_start=hex(s['sectors'][0]),
        allocated_end=hex(s['sectors'][-1]+4095),spacing=spacing,
        rmc_fix_valid=sum(r.get('fix_valid',False) for r in s['records'] if r['type_id']==2),
        previous_prefix_unchanged=True,complete=True,after_sha256=hashlib.sha256(after).hexdigest(),
        note='Cadence/counts do not by themselves prove valid advancing GPS navigation or electrical safety.')

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--before',required=True,type=Path);p.add_argument('--after',required=True,type=Path)
    p.add_argument('--duration',type=int,required=True);args=p.parse_args()
    if not 1<=args.duration<=1800:p.error('duration must be 1..1800 seconds')
    try:print(json.dumps(verify(args.before.read_bytes(),args.after.read_bytes(),args.duration),indent=2))
    except (OSError,ValueError) as exc:p.exit(2,str(exc)+'\n')
if __name__=='__main__':main()
