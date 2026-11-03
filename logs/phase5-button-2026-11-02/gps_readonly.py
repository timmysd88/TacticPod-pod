"""Read current GPS RAM configuration and observed cadence; no setting writes."""
from pathlib import Path
import json,time,serial
from serial.tools import list_ports
OUT=Path(__file__).resolve().parent
ports=[p for p in list_ports.comports() if p.vid==0x2886 and p.pid==0x8044 and p.serial_number=='0E8A89B7289430BD']
assert len(ports)==1
result={}
try:
    with (OUT/'gps-followup-transcript.txt').open('xb') as log:
        with serial.Serial(ports[0].device,115200,timeout=.05,write_timeout=1) as link:
            def read(seconds,marker=None):
                end=time.monotonic()+seconds;data=bytearray()
                while time.monotonic()<end:
                    b=link.read(max(1,link.in_waiting))
                    if b:data.extend(b);log.write(b);log.flush()
                    if marker and marker.encode() in data and b'\n' in data[data.index(marker.encode()):]:break
                s=data.decode('utf-8','replace');assert not marker or marker in s,(marker,s);return s
            def cmd(c,m,seconds):
                assert c in {'TCB STATUS','GPS CONFIG','GPS RATE','GPS STATUS'}
                log.write(('\nHOST TX '+c+'\n').encode());log.flush();link.write((c+'\n').encode());link.flush()
                return read(seconds,m)
            read(2)
            result['state']=cmd('TCB STATUS','TCB STATUS',5)
            assert all(x in result['state'] for x in ('state=1 ','error=none','lock=0'))
            result['config']=cmd('GPS CONFIG','GPS CONFIG END',40)
            result['rate']=cmd('GPS RATE','GPS RATE END',22)
            result['gps']=cmd('GPS STATUS','GPS speed_',5)
            print(result['config'],flush=True);print(result['rate'],flush=True)
finally:
    result['serial_released']=True
    (OUT/'gps-followup-result.json').write_text(json.dumps(result,indent=2)+'\n')
