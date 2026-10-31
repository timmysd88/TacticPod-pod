"""Resume read-only checks after a host assertion used a nonexistent GPS field."""
from common import *
import sys,hashlib,re
from serial.tools import list_ports
sys.path.insert(0,str(ROOT/'tools'))
from tcb3_decode import decode_packets
prior=json.loads((OUT/'upload-result.json').read_text());assert prior['uploaded'] and prior['upload_exit_code']==0
save('host-check-warning.json',{'upload_not_repeated':True,'recording_not_started':True,
    'reason':'upload.py expected profile_error=none; actual GPS STATUS omits that field when no detail exists',
    'correction':'Require 10HZ VERIFIED RAM and absence of GPS profile detail/error; verify fresh RATE window'})
ports=[p.device for p in list_ports.comports() if p.vid==0x2886 and p.pid==0x8044 and p.serial_number==SERIAL_NUMBER]
assert len(ports)==1
c=None;result={'passed':False,'serial_released':False,'recording_started':False,'reupload':False}
try:
    c=Link(ports[0],'postflight-transcript.txt');c.read(2)
    result['states']=c.states()
    assert 'reusable duration_s=120 drain_s=60 lock=0' in result['states']['TCB STATUS']
    assert 'programs=0 ' in result['states']['TCB STATUS']
    result['gps_before']=c.command('GPS STATUS','GPS speed_')
    assert '10HZ VERIFIED RAM' in result['gps_before'] and 'GPS profile detail:' not in result['gps_before'] and 'GPS ERROR:' not in result['gps_before']
    result['identity']=c.identity()
    c.command('IMU ON','IMU printing ON');samples=c.read(1.5);c.command('IMU OFF','IMU printing OFF')
    assert 'ERROR:' not in samples and samples.count('accel_g X=')>=5
    result['imu_preview']=samples
    rate=c.command('GPS RATE','GPS RATE END',20);result['gps_rate']=rate
    assert 'checksum_failed=0' in rate and 'primary=RMC' in rate and 'output_cadence_10hz=YES' in rate
    result['gps_after']=c.command('GPS STATUS','GPS speed_')
    before=int(re.search(r'checksum_fail=(\d+)',result['gps_before'])[1]);after=int(re.search(r'checksum_fail=(\d+)',result['gps_after'])[1])
    assert before==after,'New NMEA checksum failures during read-only preflight'
    result['cumulative_gps_checksum_failures']=after
    packets=c.export('after-upload-before-recording.tpx3');image,issues=decode_packets(packets)
    assert not issues and image==(OUT/'before-upload-image.bin').read_bytes()
    result['old_recordings_unchanged_after_upload']=True
    result['export_sha256']=hashlib.sha256(packets).hexdigest()
    result['passed']=True
    print('POSTFLIGHT PASS: flash fingerprint, IMU preview, fresh GPS cadence window, old recordings preserved',flush=True)
finally:
    if c:c.close();result['serial_released']=True
    save('postflight.json',result)
