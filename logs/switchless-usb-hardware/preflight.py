"""Strictly read-only installed-device preflight; never uploads or starts."""
from common import *
import sys,hashlib,zipfile,re
from serial.tools import list_ports
sys.path.insert(0,str(ROOT/'tools'))
from tcb3_decode import decode_packets,decode_image,json_document

approved='ffee03178a49e2b77130da9afdefd5f5c358c6b22ecb3e1e249b2157ca540c67'
archive=ROOT/'logs/switchless-battery-review/tacticpod_reusable_usb-switchless.zip'
review=json.loads((ROOT/'logs/switchless-battery-review/summary.json').read_text())
assert hashlib.sha256(archive.read_bytes()).hexdigest()==approved==review['artifacts']['zip']
assert review['suites_passed_current']==10 and not review['suites_failed']
assert all(hashlib.sha256((ROOT/p).read_bytes()).hexdigest()==h for p,h in review['source_hashes'].items())
results=json.loads((ROOT/'logs/switchless-battery-review/all-results.json').read_text())
assert len(results)==10 and all(v==0 for v in results.values())
with zipfile.ZipFile(archive) as z:
    manifest=json.loads(z.read('manifest.json'))['manifest'];assert set(manifest)=={'application','dfu_version'}
    raw=z.read(manifest['application']['bin_file'])
    assert b'reusable duration_s=' in raw and b'reusable finalisation deadline' in raw and b'ADC_UNVALIDATED' in raw
ports=[p for p in list_ports.comports() if p.vid==0x2886 and p.pid==0x8044 and p.serial_number==SERIAL_NUMBER]
assert len(ports)==1,'Expected exactly the known XIAO application device'
p=ports[0]
save('device.json',dict(port=p.device,vid=hex(p.vid),pid=hex(p.pid),serial=p.serial_number,description=p.description))
save('artifact-checks.json',dict(zip_sha256=approved,application_sha256=hashlib.sha256(raw).hexdigest(),manifest=manifest,source_hashes_match=True,regression_suites=10))
save('battery-confirmation.json',{'user_confirmed':'LiPo disconnected; USB-only power; serial monitor closed','basis':'Physical confirmation by user; not inferred from ADC/USB','battery_commands_not_sent':True})
c=None;result={'passed':False,'serial_released':False,'recording_started':False,'uploaded':False}
try:
    c=Link(p.device,'before-upload-transcript.txt');c.read(3)
    result['states']=c.states()
    assert 'test=120s,1MiB,180s profile=3B10 exact=B000' in result['states']['TCB STATUS']
    assert 'next_sector=0x39000' in result['states']['RECORD STATUS'],result['states']['RECORD STATUS']
    result['gps']=c.command('GPS STATUS','GPS speed_')
    result['identity']=c.identity()
    packets=c.export('before-upload.tpx3');image,issues=decode_packets(packets);assert not issues
    doc=decode_image(image);assert not doc['issues'] and [s['session_id'] for s in doc['sessions']]==[1,3,12]
    assert all(s['status']=='complete' and not s['issues'] and s['footer']['lost']==[0]*4 for s in doc['sessions'])
    archived=(ROOT/'logs/phase3b10-hardware/movement-120s-1.tpx3').read_bytes()
    old_image,old_issues=decode_packets(archived);assert not old_issues and image==old_image
    assert len(image)==0x39000
    (OUT/'before-upload-image.bin').write_bytes(image)
    result['inventory']=[dict(session_id=s['session_id'],sectors=s['sectors'],footer=s['footer']) for s in doc['sessions']]
    result['export_sha256']=hashlib.sha256(packets).hexdigest();result['image_sha256']=hashlib.sha256(image).hexdigest()
    result['archive_packet_identical']=packets==archived;result['archive_image_identical']=True;result['passed']=True
    print('PRE-FLIGHT PASS',p.device,'sessions',[(s['session_id'],s['footer']['committed']) for s in doc['sessions']],flush=True)
finally:
    if c:c.close();result['serial_released']=True
    save('preflight.json',result)
