"""ONE authorised ten-second USB-only recording; no repeat, erase or setting changes."""
from common import *
import sys,re,hashlib
from serial.tools import list_ports
sys.path.insert(0,str(ROOT/'tools'))
from tcb3_decode import decode_packets,decode_image,json_document
from verify_tcb3_append import verify
assert json.loads((OUT/'postflight.json').read_text())['passed']
ports=[p.device for p in list_ports.comports() if p.vid==0x2886 and p.pid==0x8044 and p.serial_number==SERIAL_NUMBER]
assert len(ports)==1
c=None;result={'start_command_sent':False,'started':False,'finalised':False,'verified':False,'serial_released':False}
try:
    c=Link(ports[0],'recording-transcript.txt');c.read(2)
    result['before_states']=c.states()
    assert 'programs=0 ' in result['before_states']['TCB STATUS'] and 'reusable duration_s=120 drain_s=60 lock=0' in result['before_states']['TCB STATUS']
    result['battery_before']=c.command('BAT STATUS','BAT source=')
    assert 'cell=0 calibrated=0' in result['battery_before'] and 'usb_vbus=1' in result['battery_before']
    configured=c.command('TCB DURATION 10','TCB DURATION configured')
    assert 'duration_s=10 drain_s=60 lock=0' in configured
    with (OUT/'recording-start-attempted.json').open('x') as f:
        json.dump({'one_start_only':True,'duration_s':10,'usb_remains_connected':True,'battery_absent_user_confirmed':True},f,indent=2)
    result['start_command_sent']=True
    started=c.command('TCB START','TCB STARTED',12);result['started_response']=started
    assert 'session=58 ' in started and 'duration_s=10 ' in started and 'error=none' in started
    result['started']=True;start=time.monotonic();save('recording-result.json',result)
    print('STARTED session58; USB stays connected, automatic ten-second stop',flush=True)
    polls=[]
    while time.monotonic()-start<75:
        time.sleep(.5)
        status=c.command('TCB STATUS','TCB STATUS')
        polls.append({'host_elapsed_s':round(time.monotonic()-start,3),'status':status})
        assert 'session=58 ' in status and 'error=none' in status and 'lock=0' in status
        for field in ('rejected','missed','full','crc','incomplete'):
            assert re.search(r'\b'+field+r'=0\b',status),(field,status)
        if 'state=1 ' in status:
            assert time.monotonic()-start>=9
            result['finalised']=True;result['final_status']=status;break
    result['polls']=polls;assert result['finalised'],'No finalisation by deadline; no restart/erase'
    result['host_finalisation_s']=round(time.monotonic()-start,3)
    print('Automatic finalisation complete; capturing two raw exports',flush=True)
    result['after_states']=c.states()
    result['battery_after']=c.command('BAT STATUS','BAT source=');assert 'usb_vbus=1' in result['battery_after']
    result['gps_after']=c.command('GPS STATUS','GPS speed_')
    first=c.export('after-1.tpx3');second=c.export('after-2.tpx3')
    assert first==second,'Exports differ; preserve both, no retry'
    before=(OUT/'before-upload.tpx3').read_bytes()
    verified=verify(before,first,10);result['verification']=verified
    assert verified['session_id']==58
    image,issues=decode_packets(first);assert not issues
    doc=decode_image(image);assert not doc['issues'] and len(doc['sessions'])==4
    for s in doc['sessions']:assert s['status']=='complete' and not s['issues']
    (OUT/'after-image.bin').write_bytes(image)
    result['export_bytes']=len(first);result['export_sha256']=hashlib.sha256(first).hexdigest()
    result['previous_image_unchanged']=image[:0x39000]==(OUT/'before-upload-image.bin').read_bytes()
    assert result['previous_image_unchanged']
    result['inventory']=[dict(session_id=s['session_id'],sectors=s['sectors'],footer=s['footer']) for s in doc['sessions']]
    for i,s in enumerate(doc['sessions']):
        isolated=bytearray(b'\xff'*len(image))
        for a in s['sectors']:isolated[a:a+4096]=image[a:a+4096]
        independent=decode_image(bytes(isolated));assert not independent['issues'] and len(independent['sessions'])==1
        assert independent['sessions'][0]['raw']==s['raw']
        output=dict(independent)
        for item in output['sessions']:del item['raw']
        (OUT/('session-'+str(s['session_id'])+'.json')).write_text(json.dumps(json_document(output),indent=2)+'\n')
    # Validate all samples in order, retaining exact timestamps/raw precision.
    new=doc['sessions'][-1];result['footer']=new['footer'];result['metadata']=new['metadata']
    result['verified']=True
    print('VERIFIED',verified['counts'],'export',len(first),'SHA256',result['export_sha256'],flush=True)
finally:
    if c:c.close();result['serial_released']=True
    save('recording-result.json',result)
