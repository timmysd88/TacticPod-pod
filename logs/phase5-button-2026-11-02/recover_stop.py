"""Inspect reported button-stop failure; request finalisation only, never start."""
from pathlib import Path
import json,time,re,serial
from serial.tools import list_ports

OUT=Path(__file__).resolve().parent
ports=[p for p in list_ports.comports() if p.vid==0x2886 and p.pid==0x8044 and p.serial_number=='0E8A89B7289430BD']
assert len(ports)==1, 'Device unavailable/identity uncertain; keep power, no reset'
result={'recording_started_by_host':False,'stop_sent':False,'finalised_idle':False}
try:
    with (OUT/'button-failure-transcript.txt').open('xb') as log:
        with serial.Serial(ports[0].device,115200,timeout=.05,write_timeout=1) as link:
            def read(seconds,marker=None):
                data=bytearray();end=time.monotonic()+seconds
                while time.monotonic()<end:
                    b=link.read(max(1,link.in_waiting))
                    if b:data.extend(b);log.write(b);log.flush()
                    if marker and marker.encode() in data and b'\n' in data[data.index(marker.encode()):]:break
                text=data.decode('utf-8','replace')
                assert not marker or marker in text,('response timeout',marker,text)
                return text
            def cmd(command,marker):
                assert command in {'TCB STATUS','TCB STOP','COMP STATUS','OBS STATUS','RECORD STATUS','GPS STATUS','BAT STATUS'}
                log.write(('\nHOST TX '+command+'\n').encode());log.flush()
                link.write((command+'\n').encode());link.flush()
                return read(6,marker)
            result['reconnect']=read(1)
            initial=cmd('TCB STATUS','TCB STATUS')
            result['initial_tcb']=initial
            print('INITIAL '+initial.strip(),flush=True)
            state=re.search(r'\bstate=(\d+)\b',initial)
            assert state, 'State unavailable; stop diagnosis'
            if state.group(1)!='1' or 'mode=until_stop' in initial:
                result['stop_sent']=True
                result['stop_response']=cmd('TCB STOP','TCB STOP')
                print(result['stop_response'].strip(),flush=True)
            end=time.monotonic()+65
            result['polls']=[]
            while time.monotonic()<end:
                t=cmd('TCB STATUS','TCB STATUS'); result['polls'].append(t)
                if 'state=1 ' in t and 'error=none' in t and 'lock=0' in t:
                    result['final_tcb']=t;result['finalised_idle']=True;break
                if 'lock=1' in t or 'state=8 ' in t:
                    result['final_tcb']=t;break
                time.sleep(.5)
            for command,marker in [('COMP STATUS','COMP END'),('OBS STATUS','OBS END'),('RECORD STATUS','RECORD STATUS:'),('GPS STATUS','GPS speed_'),('BAT STATUS','BAT source=')]:
                result[command]=cmd(command,marker)
            print('IDLE AFTER DIAGNOSIS:',result['finalised_idle'],flush=True)
            print(result.get('final_tcb','No idle/fault response').strip(),flush=True)
finally:
    result['serial_released']=True
    (OUT/'button-failure-result.json').write_text(json.dumps(result,indent=2)+'\n')
