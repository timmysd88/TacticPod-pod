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
    expected=json.loads((ROOT/'logs/phase3b6-review/artifacts.json').read_text())['tcb3_first_write']
    folder=ROOT/'.pio/build/tcb3_first_write'
    for suffix in ('elf','hex','zip'):
        assert hashlib.sha256((folder/('firmware.'+suffix)).read_bytes()).hexdigest()==expected[suffix]
    hashes=json.loads((ROOT/'logs/phase3b6-review/final-source-hashes.json').read_text())
    assert all(hashlib.sha256((ROOT/p).read_bytes()).hexdigest()==h for p,h in hashes.items())
    tests=json.loads((ROOT/'logs/phase3b6-review/regressions.json').read_text())
    assert len(tests)==17 and all(r['exit_code']==0 for r in tests.values())
    with zipfile.ZipFile(folder/'firmware.zip') as z:
        raw=z.read('firmware.bin');manifest=json.loads(z.read('manifest.json'))
    assert hashlib.sha256(raw).hexdigest()==expected['application_sha256']
    assert set(manifest['manifest'])=={'application','dfu_version'}
    assert b'test=5s,64KiB,15s' in raw and b'test finalisation deadline' in raw and b'TCB DISABLED:' not in raw
    save('artifact-checks.json',dict(expected,regression_suites_passed=17,test_environment='tcb3_first_write'))
    save('upload-attempted.json',{'application_only':True,'environment':'tcb3_first_write','no_automatic_repeat':True})
    if not any(p.pid==0x0045 for p in devices()):
        with serial.Serial(wait(0x8044,3),1200,timeout=.1) as p:p.dtr=False
    loader=wait(0x0045,15);print('Bootloader',loader,flush=True)
    command=[sys.executable,str(ROOT/'.pio-core/packages/tool-adafruit-nrfutil/adafruit-nrfutil.py'),
             'dfu','serial','-pkg',str(folder/'firmware.zip'),'-p',loader,'-b','115200','--singlebank']
    with (OUT/'upload.txt').open('w') as log:r=subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,timeout=90)
    assert r.returncode==0,'DFU failed';result['uploaded']=True
    c=Link(wait(0x8044,20),'preflight-transcript.txt');startup=c.read(22)
    (OUT/'startup.txt').write_text(startup)
    assert 'RECORD scan complete' in startup and 'TCB3 TEST BUILD:' in startup
    assert 'GPS PROFILE:' in startup and not bad(startup)
    print('Startup captured',flush=True)
    assert 'RECORD STATUS: STOPPED' in c.command('RECORD STATUS','RECORD STATUS:')
    c.command('OBS STOP','OBS END');c.command('COMP STOP','COMP END')
    result['states']=c.states()
    assert 'next_sector=0x0' in result['states']['RECORD STATUS']
    result['gps']=c.command('GPS STATUS','GPS speed_');assert '10HZ VERIFIED RAM' in result['gps']
    result['identity']=c.identity()
    sys.path.insert(0,str(ROOT/'tools'))
    from tcb3_decode import decode_packets,decode_image
    packets=c.export('blank-before.tpx3');image,issues=decode_packets(packets)
    assert not image and not issues and not decode_image(image)['sessions']
    result['blank_evidence']='Current header scan empty, append zero; prior full partition FF verification; firmware will verify allocated sectors in full before programming.'
    result['preflight_passed']=True;result['commands']=c.commands
    print('PASS: stopped states, bounded build, identity/SFDP/GPS and empty read-only export; no recording started.',flush=True)
except BaseException as exc:
    result['error']=repr(exc);raise
finally:
    if c:c.close();result['commands']=c.commands
    result['serial_released']=True;save('preflight-result.json',result)
