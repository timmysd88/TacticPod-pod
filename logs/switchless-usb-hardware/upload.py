"""ONE approved application-only ZIP upload; post-upload read-only validation."""
from common import *
import serial,subprocess,hashlib,sys,re
from serial.tools import list_ports
sys.path.insert(0,str(ROOT/'tools'))
from tcb3_decode import decode_packets
def devices():return [p for p in list_ports.comports() if p.vid==0x2886 and p.serial_number==SERIAL_NUMBER]
def wait(pid,seconds):
    end=time.monotonic()+seconds
    while time.monotonic()<end:
        hits=[p.device for p in devices() if p.pid==pid]
        if len(hits)==1:return hits[0]
        time.sleep(.05)
    raise RuntimeError('Enumeration timeout: '+hex(pid))

result={'uploaded':False,'postflight_passed':False,'serial_released':False,'recording_started':False}
pre=json.loads((OUT/'preflight.json').read_text());assert pre['passed'] and pre['serial_released']
confirm=json.loads((OUT/'battery-confirmation.json').read_text());assert confirm['user_confirmed'].startswith('LiPo disconnected;')
archive=ROOT/'logs/switchless-battery-review/tacticpod_reusable_usb-switchless.zip'
assert hashlib.sha256(archive.read_bytes()).hexdigest()=='ffee03178a49e2b77130da9afdefd5f5c358c6b22ecb3e1e249b2157ca540c67'
c=None
try:
    with (OUT/'upload-attempted.json').open('x') as f:
        json.dump({'one_attempt':True,'application_only':True,'battery_absent_user_confirmed':True,'zip_sha256':hashlib.sha256(archive.read_bytes()).hexdigest()},f,indent=2)
    application=wait(0x8044,3)
    with serial.Serial(application,1200,timeout=.1) as touch:touch.dtr=False
    loader=wait(0x0045,15);result['bootloader_port']=loader
    print('Uploading approved ZIP via',loader,flush=True)
    command=[sys.executable,str(ROOT/'.pio-core/packages/tool-adafruit-nrfutil/adafruit-nrfutil.py'),
        'dfu','serial','-pkg',str(archive),'-p',loader,'-b','115200','--singlebank']
    result['upload_command']=command
    with (OUT/'upload.txt').open('w') as log:
        p=subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,timeout=90)
    result['upload_exit_code']=p.returncode;assert p.returncode==0,'Upload failed; no retry'
    result['uploaded']=True;app=wait(0x8044,20);result['application_port']=app
    print('Application enumerated at',app,'capturing startup',flush=True)
    c=Link(app,'after-upload-transcript.txt');startup=c.read(35)
    (OUT/'startup.txt').write_text(startup)
    assert 'RECORD scan complete' in startup and 'ERROR: LSM6' not in startup
    result['states']=c.states()
    assert 'reusable duration_s=120 drain_s=60 lock=0' in result['states']['TCB STATUS']
    assert 'programs=0 ' in result['states']['TCB STATUS']
    assert 'next_sector=0x39000' in result['states']['RECORD STATUS']
    result['battery']=c.command('BAT STATUS','BAT source=')
    assert 'source=ADC_UNVALIDATED cell=0 calibrated=0' in result['battery'] and 'trusted=0' in result['battery'] and 'usb_vbus=1' in result['battery']
    result['gps']=c.command('GPS STATUS','GPS speed_')
    assert '10HZ VERIFIED RAM' in result['gps'] and 'profile_error=none' in result['gps']
    result['identity']=c.identity()
    c.command('IMU ON','IMU printing ON');samples=c.read(1.5)
    c.command('IMU OFF','IMU printing OFF')
    assert 'ERROR:' not in samples and samples.count('accel_g X=')>=5
    result['imu_preview']=samples
    packets=c.export('after-upload-before-recording.tpx3');image,issues=decode_packets(packets)
    assert not issues and image==(OUT/'before-upload-image.bin').read_bytes()
    result['old_recordings_unchanged_after_upload']=True
    result['postflight_passed']=True
    print('POST-UPLOAD PASS: reusable idle, GPS 10Hz, IMU preview, flash fingerprint, previous recordings intact',flush=True)
finally:
    if c:c.close();result['serial_released']=True
    save('upload-result.json',result)
