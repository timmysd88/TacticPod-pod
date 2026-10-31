"""Exactly one authorized bounded recording. No erase, retry or manual restart."""
from common import *
import hashlib,sys,re
from serial.tools import list_ports
result={'start_commands_sent':0,'automatic_stop_observed':False,'exports':[],'errors':[],'serial_released':False}
c=None
try:
    assert json.loads((OUT/'preflight-result.json').read_text())['preflight_passed']
    assert not (OUT/'record-attempted.json').exists(),'One-shot latch exists: never repeat'
    ports=[p for p in list_ports.comports() if p.serial_number==SERIAL_NUMBER and p.vid==0x2886 and p.pid==0x8044]
    assert len(ports)==1
    c=Link(ports[0].device,'record-transcript.txt');c.read(.5)
    result['before']=c.states();assert 'next_sector=0x0' in result['before']['RECORD STATUS']
    result['gps_before']=c.command('GPS STATUS','GPS speed_');assert '10HZ VERIFIED RAM' in result['gps_before']
    save('record-attempted.json',{'authorised_start_count':1,'capture_seconds':5,'write_end_exclusive':65536,'no_retry':True})
    host_start=time.monotonic();c.send('TCB START');result['start_commands_sent']=1
    started=c.read(12,'TCB STARTED');result['started_response']=started
    assert 'test=5s,64KiB,15s' in started and 'error=none' in started
    print(started,flush=True)
    began=time.monotonic();next_status=began+.8;next_comp=began+1.5;pending='';reports=[]
    while time.monotonic()-began<20:
        text=c.read(.1);pending+=text
        if 'RECORD scan complete' in text or 'GPS STARTUP:' in text:raise RuntimeError('Unexpected reset during recording')
        if 'TCB STOPPED' in pending:
            result['automatic_stop_observed']=True;break
        if time.monotonic()>=next_status:
            status=c.command('TCB STATUS','TCB STATUS');reports.append({'host_elapsed_s':time.monotonic()-began,'text':status});pending+=status
            if 'TCB STOPPED' in pending:result['automatic_stop_observed']=True;break
            for key in ('rejected','missed','full','crc','incomplete'):
                if any(int(n) for n in re.findall(key+r'=(\d+)',status)):raise RuntimeError('Loss/failure counter: '+status)
            assert 'error=none' in status,status
            next_status=time.monotonic()+.8
        if time.monotonic()>=next_comp:
            comp=c.command('COMP STATUS','COMP END');reports.append({'host_elapsed_s':time.monotonic()-began,'text':comp});pending+=comp
            assert 'fault=0' in comp and 'rejected=0' in comp,comp
            next_comp=time.monotonic()+1.5
        pending=pending[-4096:]
    result['monitor']=reports;result['host_elapsed_start_to_stop_s']=time.monotonic()-host_start
    assert result['automatic_stop_observed'],'No clean automatic stop within deadline'
    result['stop_notice']=pending
    # The legacy append-cache rescan is read-only; wait for its notice/idle state.
    result['settling']=c.read(2)
    result['after']=c.states()
    status=result['after']['TCB STATUS']
    assert 'state=1' in status and 'error=none' in status,status
    for key in ('rejected','missed','full','crc','incomplete'):
        assert not any(int(n) for n in re.findall(key+r'=(\d+)',status)),status
    print('Automatic STOPPED; final counters clean. Capturing two binary exports.',flush=True)
    sys.path.insert(0,str(ROOT/'tools'))
    from tcb3_decode import decode_packets,decode_image
    for n in (1,2):
        name=f'first-write-{n}.tpx3';data=c.export(name)
        image,issues=decode_packets(data);assert not issues
        decoded=decode_image(image);assert not decoded['issues'] and len(decoded['sessions'])==1
        s=decoded['sessions'][0];assert s['status']=='complete' and not s['issues']
        assert s['footer']['reason']==0 and s['footer']['lost']==[0]*4
        assert max(s['sectors'])+4096<=65536
        result['exports'].append({'file':name,'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest()})
        print('Export',n,len(data),'bytes',flush=True)
    assert (OUT/'first-write-1.tpx3').read_bytes()==(OUT/'first-write-2.tpx3').read_bytes(),'Exports differ'
    result['final']=c.states();result['gps_after']=c.command('GPS STATUS','GPS speed_')
    result['status']='complete'
except BaseException as exc:
    result['status']='failed';result['errors'].append(repr(exc));raise
finally:
    if c:c.close();result['commands']=c.commands
    result['serial_released']=True;save('record-result.json',result)
    print('Serial released; no erase or retry issued.',flush=True)
