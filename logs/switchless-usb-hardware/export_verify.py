"""Read-only completion after the expected post-finalisation legacy rescan."""
from common import *
import sys,hashlib,re
from serial.tools import list_ports
sys.path.insert(0,str(ROOT/'tools'))
from tcb3_decode import decode_packets,decode_image,json_document
from verify_tcb3_append import verify
prior=json.loads((OUT/'recording-result.json').read_text())
assert prior['started'] and prior['finalised'] and not prior['verified']
ports=[p.device for p in list_ports.comports() if p.vid==0x2886 and p.pid==0x8044 and p.serial_number==SERIAL_NUMBER]
assert len(ports)==1
c=None;result={'verified':False,'serial_released':False,'new_recording_started':False}
try:
    c=Link(ports[0],'verification-transcript.txt');c.read(2)
    for n in range(20):
        state=c.command('RECORD STATUS','RECORD STATUS:')
        if 'RECORD STATUS: STOPPED' in state:break
        assert 'SCANNING' in state,'Unexpected recorder state'
        time.sleep(.5)
    else:raise RuntimeError('Legacy rescan did not complete; preserve data')
    result['states']=c.states()
    status=result['states']['TCB STATUS']
    assert 'session=58 ' in status and 'reusable duration_s=10 drain_s=60 lock=0' in status
    for field in ('rejected','missed','full','crc','incomplete'):assert re.search(r'\b'+field+r'=0\b',status)
    result['battery']=c.command('BAT STATUS','BAT source=');assert 'usb_vbus=1' in result['battery']
    result['gps']=c.command('GPS STATUS','GPS speed_')
    first=c.export('after-1.tpx3');second=c.export('after-2.tpx3')
    assert first==second,'Export mismatch; no retry'
    result['export_bytes']=len(first);result['export_sha256']=hashlib.sha256(first).hexdigest()
    image,issues=decode_packets(first);assert not issues
    doc=decode_image(image);assert not doc['issues'] and [s['session_id'] for s in doc['sessions']]==[1,3,12,58]
    assert all(s['status']=='complete' and not s['issues'] for s in doc['sessions'])
    (OUT/'after-image.bin').write_bytes(image)
    result['previous_image_unchanged']=image[:0x39000]==(OUT/'before-upload-image.bin').read_bytes()
    assert result['previous_image_unchanged']
    result['inventory']=[dict(session_id=s['session_id'],sectors=s['sectors'],footer=s['footer']) for s in doc['sessions']]
    for s in doc['sessions']:
        isolated=bytearray(b'\xff'*len(image))
        for a in s['sectors']:isolated[a:a+4096]=image[a:a+4096]
        independent=decode_image(bytes(isolated));assert not independent['issues'] and len(independent['sessions'])==1
        assert independent['sessions'][0]['raw']==s['raw']
        for r in independent['sessions']:del r['raw']
        (OUT/('session-'+str(s['session_id'])+'.json')).write_text(json.dumps(json_document(independent),indent=2)+'\n')
    result['verification']=verify((OUT/'before-upload.tpx3').read_bytes(),first,10)
    result['footer']=doc['sessions'][-1]['footer'];result['metadata']=doc['sessions'][-1]['metadata']
    result['verified']=True
    print('PASS: two identical exports',len(first),'bytes SHA256',result['export_sha256'],flush=True)
    print('PASS: old recordings unchanged, independent decode, counts',result['footer']['committed'],flush=True)
finally:
    if c:c.close();result['serial_released']=True
    save('verification-result.json',result)
