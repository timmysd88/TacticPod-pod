from common import *
import hashlib,sys,re
from serial.tools import list_ports
sys.path.insert(0,str(ROOT/'tools'))
from tcb3_decode import decode_packets,decode_image
result={'start_commands_sent':0,'automatic_stop_observed':False,'exports':[],'errors':[],'serial_released':False,'full_allocation_independently_prescanned':False}
c=None

def check_status(t):
    assert not bad(t),t
    for key in ('rejected','missed','full','crc','incomplete'):
        assert not any(int(n) for n in re.findall(key+r'=(\d+)',t)),t
    assert 'error=none' in t,t
    assert 'session=3 ' in t and 'skip=0' in t,t

try:
    assert json.loads((OUT/'preflight-result.json').read_text())['preflight_passed']
    assert not (OUT/'record-attempted.json').exists(),'One-shot latch exists: never repeat'
    hashes=json.loads((ROOT/'logs/phase3b9-review/final-source-hashes.json').read_text())
    assert all(hashlib.sha256((ROOT/p).read_bytes()).hexdigest()==h for p,h in hashes.items())
    ports=[p for p in list_ports.comports() if p.serial_number==SERIAL_NUMBER and p.vid==0x2886 and p.pid==0x8044]
    assert len(ports)==1
    c=Link(ports[0].device,'record-transcript.txt');c.read(.5)
    result['before']=c.states();assert 'next_sector=0x2000' in result['before']['RECORD STATUS']
    assert 'state=1 session=0' in result['before']['TCB STATUS'] and 'programs=0' in result['before']['TCB STATUS']
    result['identity_before']=c.identity()
    result['gps_before']=c.command('GPS STATUS','GPS speed_');assert '10HZ VERIFIED RAM' in result['gps_before']
    old_bytes=c.export('before-start-old.tpx3')
    assert old_bytes==(ROOT/'logs/phase3b7-hardware/first-write-1.tpx3').read_bytes()
    old_image,issues=decode_packets(old_bytes);old=decode_image(old_image)
    assert not issues and not old['issues'] and len(old['sessions'])==1
    assert old['sessions'][0]['status']=='complete' and old['sessions'][0]['footer']['committed']==[500,50,5,0]
    result['old_export_archive_match_before_start']=True
    save('record-attempted.json',{'authorised_start_count':1,'capture_seconds':30,'total_deadline_seconds':60,'write_start':8192,'write_end_exclusive':262144,'no_retry':True})
    host_start=time.monotonic();c.send('TCB START');result['start_commands_sent']=1
    started=c.read(12,'TCB STARTED');result['started_response']=started
    check_status(started)
    assert 'profile=3B9 exact=2000' in started,started
    result['first_sector_probe_verified']='STARTED requires exact 0x2000, validated predecessor headers and all 4096 initial-sector bytes FF; inferred from reviewed firmware, not independent raw readback.'
    print(started,flush=True)
    began=time.monotonic();next_status=began+1;next_comp=began+3;pending=started;reports=[]
    result['monitor']=reports
    while time.monotonic()-began<65:
        text=c.read(.12);pending+=text
        if 'RECORD scan complete' in text or 'GPS STARTUP:' in text or 'TacticPod command menu' in text:raise RuntimeError('Unexpected reset during recording')
        if 'TCB STOPPED' in pending:
            result['automatic_stop_observed']=True;break
        if time.monotonic()>=next_status:
            status=c.command('TCB STATUS','TCB STATUS');reports.append({'host_elapsed_s':time.monotonic()-began,'text':status});pending+=status
            check_status(status)
            if 'TCB STOPPED' in pending:result['automatic_stop_observed']=True;break
            next_status=time.monotonic()+1
        if time.monotonic()>=next_comp:
            comp=c.command('COMP STATUS','COMP END');reports.append({'host_elapsed_s':time.monotonic()-began,'text':comp});pending+=comp
            assert 'fault=0' in comp and 'rejected=0' in comp,comp
            next_comp=time.monotonic()+3
        pending=pending[-8192:]
    result['host_elapsed_start_to_stop_s']=time.monotonic()-host_start
    assert result['automatic_stop_observed'],'No clean automatic stop within deadline'
    result['stop_notice']=pending;result['settling']=c.read(2)
    result['after']=c.states();status=result['after']['TCB STATUS'];check_status(status)
    assert 'state=1' in status,status
    print('Automatic STOPPED; counters clean. Capturing two binary exports.',flush=True)
    for n in (1,2):
        name=f'append-30s-{n}.tpx3';data=c.export(name)
        image,issues=decode_packets(data);assert not issues
        decoded=decode_image(image);assert not decoded['issues'] and len(decoded['sessions'])==2
        previous,s=decoded['sessions']
        assert previous['session_id']==1 and previous['status']=='complete' and previous['raw']==old['sessions'][0]['raw']
        assert image[:8192]==old_image[:8192],'Old sector bytes changed'
        assert s['session_id']==3 and s['sectors'][0]==8192 and s['status']=='complete' and not s['issues']
        assert s['footer']['reason']==0 and s['footer']['lost']==[0]*4
        assert all(s['footer'][k]==0 for k in ('rejected','crc_failures','incomplete_writes','full','missed'))
        assert max(s['sectors'])+4096<=262144
        result['exports'].append({'file':name,'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest()})
        print('Export',n,len(data),'bytes, new counts',s['footer']['committed'],flush=True)
    assert (OUT/'append-30s-1.tpx3').read_bytes()==(OUT/'append-30s-2.tpx3').read_bytes(),'Exports differ'
    result['final']=c.states();check_status(result['final']['TCB STATUS'])
    result['gps_after']=c.command('GPS STATUS','GPS speed_')
    result['identity_after']=c.identity()
    result['status']='complete'
except BaseException as exc:
    result['status']='failed';result['errors'].append(repr(exc))
    # Stop is permitted to end the sole authorised attempt; never retry/reset/erase.
    if c and result['start_commands_sent'] and not result['automatic_stop_observed']:
        try:c.send('TCB STOP');result['emergency_stop_response']=c.read(2)
        except BaseException as stop_error:result['emergency_stop_error']=repr(stop_error)
    raise
finally:
    if c:c.close();result['commands']=c.commands
    result['serial_released']=True;save('record-result.json',result)
    print('Serial released; no erase or retry issued.',flush=True)
