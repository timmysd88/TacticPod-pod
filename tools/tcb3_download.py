"""Explicit read-only TCB READ download. Never uploads, starts, stops or erases."""
from pathlib import Path
import argparse,hashlib,struct,time,zlib
try:
    from .tcb3_decode import decode_packets
except ImportError:
    from tcb3_decode import decode_packets

def capture(link,target,timeout=300):
    """Caller supplies an open 115200-baud stream and an exclusive binary file."""
    deadline=time.monotonic()+timeout;sequence=0;total=0;digest=hashlib.sha256()
    link.write(b'TCB READ\n');link.flush() # Only transmitted command.
    def exact(n):
        nonlocal total
        data=bytearray()
        while len(data)<n:
            if time.monotonic()>=deadline:raise TimeoutError('TPX3 timeout; partial export retained; no retry')
            chunk=link.read(n-len(data))
            if chunk:target.write(chunk);digest.update(chunk);total+=len(chunk);data.extend(chunk)
        return bytes(data)
    while True:
        h=exact(20)
        if h[:4]!=b'TPX3' or h[4]!=1:raise ValueError('Expected TPX3; stop and inspect device state')
        n=struct.unpack_from('<H',h,16)[0]
        if n>256:raise ValueError('TPX3 payload exceeds page bound')
        tail=exact(n+4)
        if struct.unpack_from('<I',h,8)[0]!=sequence:raise ValueError('TPX3 sequence mismatch')
        if zlib.crc32(h+tail[:-4])!=struct.unpack_from('<I',tail,n)[0]:raise ValueError('TPX3 CRC mismatch')
        sequence+=1
        if h[5]==3:raise ValueError('TPX3 ISSUE reported; preserve partial capture and inspect')
        if h[5]==4:break
    target.flush()
    return dict(bytes=total,sha256=digest.hexdigest(),packets=sequence)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port',required=True)
    parser.add_argument('--output',required=True,type=Path)
    parser.add_argument('--timeout',type=float,default=300)
    args=parser.parse_args()
    if args.timeout<=0:parser.error('timeout must be positive')
    try:
        # Refuse existing evidence before opening a serial device. Import and
        # connect only when this CLI is explicitly invoked, never at import.
        with args.output.open('xb') as target:
            import serial
            with serial.Serial(args.port,115200,timeout=.1,write_timeout=1) as link:
                # Drain the reconnect menu before asking for binary data. There
                # is no line-rate change, 1200-baud touch, extra command or retry.
                until=time.monotonic()+2
                while time.monotonic()<until:link.read(max(1,link.in_waiting))
                result=capture(link,target,args.timeout)
        _,issues=decode_packets(args.output.read_bytes())
        if issues:raise ValueError('TPX3 export contains ISSUE packets')
        print(f"Saved {result['bytes']} bytes; SHA-256 {result['sha256']}; serial released")
    except (OSError,ValueError,TimeoutError) as exc:parser.exit(2,str(exc)+'\n')

if __name__=='__main__':main()
