"""Capture post-test state/events; safely stop only if recording still active."""
from pathlib import Path
import serial,time,json,re
from serial.tools import list_ports
OUT=Path(__file__).resolve().parent
ports=[p for p in list_ports.comports() if p.vid==0x2886 and p.pid==0x8044 and p.serial_number=='0E8A89B7289430BD']
assert len(ports)==1,'Identity/port uncertain; no commands sent'
result={'host_start_sent':False,'host_stop_sent':False,'idle_confirmed':False}
try:
    with (OUT/'post-test-state-events-transcript.txt').open('xb') as log:
        with serial.Serial(ports[0].device,115200,timeout=.05,write_timeout=1) as link:
            def read(seconds,marker=None):
                end=time.monotonic()+seconds;data=bytearray()
                while time.monotonic()<end:
                    b=link.read(max(1,link.in_waiting))
                    if b:data.extend(b);log.write(b);log.flush()
                    if marker and marker.encode() in data and b'\n' in data[data.index(marker.encode()):]:break
                s=data.decode('utf-8','replace');assert not marker or marker in s,(marker,s);return s
            def send(c):
                assert c in {'TCB STATUS','TCB STOP','FIELD EVENTS','RECORD STATUS','OBS STATUS','COMP STATUS','GPS STATUS','BAT STATUS'}
                log.write(('\nHOST TX '+c+'\n').encode());log.flush();link.write((c+'\n').encode());link.flush()
            def command(c,m):send(c);return read(8,m)
            result['reconnect_output']=read(2)
            result['initial_tcb']=command('TCB STATUS','TCB STATUS')
            print(result['initial_tcb'].strip(),flush=True)
            state=re.search(r'\bstate=(\d+)\b',result['initial_tcb']);assert state
            if (state[1]!='1' or 'mode=until_stop' in result['initial_tcb']) and 'lock=1' not in result['initial_tcb']:
                result['host_stop_sent']=True
                result['host_stop_response']=command('TCB STOP','TCB STOP')
                print('Host requested safe finalisation of still-active test; initial state retained.',flush=True)
            result['polls']=[];end=time.monotonic()+65
            while time.monotonic()<end:
                s=command('TCB STATUS','TCB STATUS');result['polls'].append(s)
                if all(p in s for p in ('state=1 ','lock=0','error=none')):
                    result['idle_confirmed']=True;break
                if 'lock=1' in s or 'state=8 ' in s:break
                time.sleep(.5)
            send('FIELD EVENTS')
            end=time.monotonic()+40;data=bytearray();last=time.monotonic();seen=False
            while time.monotonic()<end:
                b=link.read(max(1,link.in_waiting))
                if b:
                    data.extend(b);log.write(b);log.flush();last=time.monotonic()
                    if b'FIELD t_ms=' in data:seen=True
                if seen and time.monotonic()-last>2:break
            result['event_output']=data.decode('utf-8','replace')
            result['events']=[dict(ms=int(m[0]),event=m[1],session=int(m[2]),detail=int(m[3]),overwritten=int(m[4])) for m in re.findall(r'FIELD t_ms=(\d+) event=(\w+) session=(\d+) detail=(\d+) overwritten=(\d+)',result['event_output'])]
            for c,m in [('RECORD STATUS','RECORD STATUS:'),('COMP STATUS','COMP END'),('OBS STATUS','OBS END'),('BAT STATUS','BAT source='),('GPS STATUS','GPS speed_')]:result[c]=command(c,m)
            print(json.dumps(result['events'],indent=2),flush=True)
finally:
    result['serial_released']=True
    (OUT/'post-test-state-events.json').write_text(json.dumps(result,indent=2)+'\n')
