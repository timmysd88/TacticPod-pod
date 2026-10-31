"""Authorized dedicated ZIP upload and strictly read-only blank preflight."""
from common import *
import sys,subprocess,hashlib,zipfile
from serial.tools import list_ports
import serial
result={'uploaded':False,'preflight_passed':False,'recording_started':False,'serial_released':False}
def devices():return [p for p in list_ports.comports() if p.serial_number==SERIAL_NUMBER and p.vid==0x2886]
def wait(pid,seconds):
    end=time.monotonic()+seconds
    while time.monotonic()<end:
        for p in devices():
            if p.pid==pid:return p.device
        time.sleep(.05)
    raise RuntimeError('USB enumeration timeout '+hex(pid))
c=None
try:
    assert not (OUT/'upload-attempted.json').exists(),'No automatic re-upload'
    expected=json.loads((ROOT/'logs/phase3b10-review/artifacts.json').read_text())['tcb3_movement_120s']
    folder=ROOT/'.pio/build/tcb3_movement_120s'
    archive=ROOT/'logs/phase3b10-review/tcb3_movement_120s-3b10.zip'
    assert hashlib.sha256(archive.read_bytes()).hexdigest()=='0bdd99698bd4535dfe02943a045d3c9b697a4f5f3d92b077857d7f4f8d9c059e'
    assert hashlib.sha256(archive.read_bytes()).hexdigest()==expected['zip']
    for suffix in ('elf','hex','zip'):
        assert hashlib.sha256((folder/('firmware.'+suffix)).read_bytes()).hexdigest()==expected[suffix]
    hashes=json.loads((ROOT/'logs/phase3b10-review/final-source-hashes.json').read_text())
    assert all(hashlib.sha256((ROOT/p).read_bytes()).hexdigest()==h for p,h in hashes.items())
    tests=json.loads((ROOT/'logs/phase3b10-review/regressions.json').read_text())
    assert len(tests)==20 and all(r['exit_code']==0 for r in tests.values())
    with zipfile.ZipFile(archive) as z:
        raw=z.read('firmware.bin');manifest=json.loads(z.read('manifest.json'))
    assert hashlib.sha256(raw).hexdigest()==expected['application_sha256']
    assert set(manifest['manifest'])=={'application','dfu_version'}
    assert b'test=120s,1MiB,180s profile=3B10 exact=B000' in raw and b'test finalisation deadline' in raw and b'TCB DISABLED:' not in raw
    save('artifact-checks.json',dict(expected,regression_suites_passed=20,test_environment='tcb3_movement_120s'))
    save('upload-attempted.json',{'application_only':True,'environment':'tcb3_movement_120s','no_automatic_repeat':True})
    if not any(p.pid==0x0045 for p in devices()):
        with serial.Serial(wait(0x8044,3),1200,timeout=.1) as p:p.dtr=False
    loader=wait(0x0045,15);print('Bootloader',loader,flush=True)
    command=[sys.executable,str(ROOT/'.pio-core/packages/tool-adafruit-nrfutil/adafruit-nrfutil.py'),
             'dfu','serial','-pkg',str(archive),'-p',loader,'-b','115200','--singlebank']
    with (OUT/'upload.txt').open('w') as log:r=subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,timeout=90)
    assert r.returncode==0,'DFU failed';result['uploaded']=True
    c=Link(wait(0x8044,20),'preflight-transcript.txt');startup=c.read(22)
    (OUT/'startup.txt').write_text(startup)
    assert 'RECORD scan complete' in startup and 'TCB3 TEST BUILD profile 3B10:' in startup
    assert 'GPS PROFILE:' in startup and not bad(startup)
    print('Startup captured',flush=True)
    assert 'RECORD STATUS: STOPPED' in c.command('RECORD STATUS','RECORD STATUS:')
    c.command('OBS STOP','OBS END');c.command('COMP STOP','COMP END')
    result['states']=c.states()
    assert 'next_sector=0xB000' in result['states']['RECORD STATUS']
    result['gps']=c.command('GPS STATUS','GPS speed_');assert '10HZ VERIFIED RAM' in result['gps']
    result['identity']=c.identity()
    sys.path.insert(0,str(ROOT/'tools'))
    from tcb3_decode import decode_packets,decode_image
    packets=c.export('before-1.tpx3');image,issues=decode_packets(packets)
    assert not issues
    decoded=decode_image(image)
    assert not decoded['issues'] and len(decoded['sessions'])==2
    old=decoded['sessions'][0]
    recent=decoded['sessions'][1]
    assert recent['session_id']==3 and recent['status']=='complete' and not recent['issues']
    assert recent['footer']['committed']==[3000,300,30,0] and recent['footer']['lost']==[0]*4
    assert old['status']=='complete' and not old['issues']
    assert old['sectors']==[0,4096],old['sectors']
    archived=(ROOT/'logs/phase3b9-corrected-hardware/append-30s-1.tpx3').read_bytes()
    assert packets==archived,'Old TPX3 export differs from archived export'
    assert c.export('before-2.tpx3')==packets,'Repeated old export differs'
    result['old_export']={'bytes':len(packets),'sha256':hashlib.sha256(packets).hexdigest(),'archive_byte_identical':True,'repeat_byte_identical':True}
    result['old_session_status']=old['status']
    result['read_only_checks_passed']=True
    result['preflight_passed']=True
    result['first_sector_verification']='The corrected firmware must completely probe 0xB000 before STARTED; no independent allocation pre-scan. Relies on previously approved archived erase plus just-in-time checks.'
    assert old['footer']['committed']==[500,50,5,0] and old['footer']['lost']==[0]*4
    assert old['raw']==(ROOT/'logs/phase3b7-hardware/first-write.v2wire').read_bytes()
    assert image[:0xb000]==(ROOT/'logs/phase3b9-corrected-hardware/recovered-recording-image.bin').read_bytes()
    assert 'state=1 session=0' in result['states']['TCB STATUS'] and 'programs=0' in result['states']['TCB STATUS']
    assert '16777216 bytes' in startup
    result['final_states']=c.states()
    print('PASS: exact reviewed upload, normal boot, GPS/identity/stopped states, append 0xB000 and old archive unchanged.',flush=True)
except BaseException as exc:
    result['error']=repr(exc);raise
finally:
    if c:c.close();result['commands']=c.commands
    result['serial_released']=True;save('preflight-result.json',result)
