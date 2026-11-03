"""One USB-connected physical button trial; status/read commands only."""
from pathlib import Path
import serial,time,json,re,sys,hashlib
from serial.tools import list_ports
ROOT=Path(__file__).resolve().parents[2];OUT=Path(__file__).resolve().parent
sys.path.insert(0,str(ROOT/'tools'))
from tcb3_download import capture
from tcb3_decode import decode_packets,decode_image
before_image,_=decode_packets((OUT/'before-test.tpx3').read_bytes())
before=decode_image(before_image);old_ids={s['session_id'] for s in before['sessions']}
ports=[p for p in list_ports.comports() if p.vid==0x2886 and p.pid==0x8044 and p.serial_number=='0E8A89B7289430BD']
assert len(ports)==1
result={'status':'preparing','host_start_sent':False,'host_stop_sent':False,'port':ports[0].device,'polls':[]}
def save():
    (OUT/'monitor-result.json').write_text(json.dumps(result,indent=2)+'\n')
try:
    with (OUT/'live-transcript.txt').open('xb') as log:
        with serial.Serial(ports[0].device,115200,timeout=.05,write_timeout=1) as link:
            def read(seconds,marker=None):
                end=time.monotonic()+seconds;data=bytearray()
                while time.monotonic()<end:
                    b=link.read(max(1,link.in_waiting))
                    if b:data.extend(b);log.write(b);log.flush()
                    if marker and marker.encode() in data and b'\n' in data[data.index(marker.encode()):]:break
                s=data.decode('utf-8','replace');assert not marker or marker in s,(marker,s);return s
            def send(c):
                assert c in {'TCB STATUS','FIELD EVENTS','RECORD STATUS','COMP STATUS','OBS STATUS','GPS STATUS','BAT STATUS'}
                log.write(('\nHOST TX '+c+'\n').encode());log.flush();link.write((c+'\n').encode());link.flush()
            def command(c,m):send(c);return read(8,m)
            def events():
                send('FIELD EVENTS');end=time.monotonic()+35;data=bytearray();last=time.monotonic();seen=False
                while time.monotonic()<end:
                    b=link.read(max(1,link.in_waiting))
                    if b:
                        data.extend(b);log.write(b);log.flush();last=time.monotonic()
                        if b'FIELD t_ms=' in data:seen=True
                    if seen and time.monotonic()-last>2:break
                return data.decode('utf-8','replace')
            read(2)
            result['prestart']=command('TCB STATUS','TCB STATUS')
            assert all(x in result['prestart'] for x in ('state=1 ','lock=0','error=none'))
            result['baseline_events']=events()
            result['status']='waiting_for_physical_start';save()
            print('READY: USB remains connected. Status-only live monitor active; no START/STOP transmitted.',flush=True)
            end=time.monotonic()+300;session=None
            while time.monotonic()<end and not (OUT/'stop-monitor').exists():
                s=command('TCB STATUS','TCB STATUS');result['polls'].append(s)
                m=re.search(r'TCB STATUS state=(\d+) session=(\d+)',s)
                if m:
                    state,sid=map(int,m.groups())
                    if sid not in old_ids and sid>0 and state!=1:
                        session=sid;result['session']=sid;result['status']='recording_or_finalising'
                    if session is not None and sid==0 and state==1:
                        result['status']='unexpected_restart_suspected';break
                    if 'lock=1' in s or state==8:
                        result['status']='fault';break
                    if session is not None and sid==session and state==1 and 'lock=0' in s and 'error=none' in s:
                        result['status']='clean_idle_observed';result['final_tcb']=s;break
                save();time.sleep(1.8)
            else:result['status']='monitor_timeout_or_cancelled'
            save()
            if result['status']=='clean_idle_observed':
                result['post_events']=events()
                for c,m in [('RECORD STATUS','RECORD STATUS:'),('COMP STATUS','COMP END'),('OBS STATUS','OBS END'),('BAT STATUS','BAT source='),('GPS STATUS','GPS speed_')]:result[c]=command(c,m)
                read(.5)
                with (OUT/'after-test.tpx3').open('xb') as target:result['archive']=capture(link,target,120)
    if (OUT/'after-test.tpx3').exists():
        image,issues=decode_packets((OUT/'after-test.tpx3').read_bytes());doc=decode_image(image)
        old={s['session_id']:s for s in before['sessions']};current={s['session_id']:s for s in doc['sessions']}
        result['old_sessions_preserved']=image[:len(before_image)]==before_image and all(current.get(k)==v for k,v in old.items())
        result['new_sessions']=[{'id':s['session_id'],'status':s['status'],'start_us':s['metadata']['start_us'],'footer':s['footer']} for s in doc['sessions'] if s['session_id'] not in old]
        result['export_issues']=issues;result['container_issues']=doc['issues']
        result['status']='analysis_ready'
        print('Recording stopped; archive saved; previous sessions preserved:',result['old_sessions_preserved'],flush=True)
except Exception as e:
    result['error']=str(e);result['status']='monitor_error';print('MONITOR ERROR:',e,flush=True)
finally:
    result['serial_released']=True;save()
