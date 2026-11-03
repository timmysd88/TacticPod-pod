"""One owner-requested application DFU, existing bootloader; never START."""
from pathlib import Path
import json,hashlib,zipfile,subprocess,sys,time,serial
from serial.tools import list_ports
ROOT=Path(__file__).resolve().parents[2];OUT=Path(__file__).resolve().parent
NUMBER='0E8A89B7289430BD'
ZIP='c38561e5004ec88bf5d1b6b8b21c4d50a55cbbd19db41a7065c804438a6ae007'
BIN='40bced328dbcc453eb8326297fabab40c268063140d6c909a4359b4024a9a585'
archive=OUT/'firmware.zip'
assert hashlib.sha256(archive.read_bytes()).hexdigest()==ZIP
with zipfile.ZipFile(archive) as z:
    m=json.loads(z.read('manifest.json'))['manifest']
    assert set(m)=={'application','dfu_version'}
    assert m['application']['init_packet_data']['softdevice_req']==[0x123]
    assert hashlib.sha256(z.read(m['application']['bin_file'])).hexdigest()==BIN
pre=json.loads((OUT/'preservation-result.json').read_text())
assert pre['passed'] and pre['serial_released']
assert hashlib.sha256((OUT/'before-upload.tpx3').read_bytes()).hexdigest()==pre['archive']['sha256']
assert subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip()=='155a999122f0ce85ab6817c6eba690e9501fe4b3'
def wait(pid,seconds):
    end=time.monotonic()+seconds
    while time.monotonic()<end:
        hits=[p for p in list_ports.comports() if p.vid==0x2886 and p.pid==pid and p.serial_number==NUMBER]
        if len(hits)==1:return hits[0].device
        time.sleep(.1)
    raise RuntimeError('Enumeration timeout; no retry: '+hex(pid))
result={'uploaded':False,'application_only':True,'recording_started':False,'calibration_changed':False,'zip_sha256':ZIP,'bin_sha256':BIN}
application=wait(0x8044,2)
with (OUT/'upload-attempted.json').open('x') as f:
    json.dump(dict(result,approval='Owner requested merged firmware upload and one user-performed 30-second test; physical preconditions reconfirmed'),f,indent=2)
try:
    with serial.Serial(application,1200,timeout=.1) as touch:touch.dtr=False
    loader=wait(0x0045,20);result['bootloader_port']=loader
    command=[sys.executable,str(ROOT/'.pio-core/packages/tool-adafruit-nrfutil/adafruit-nrfutil.py'),
             'dfu','serial','-pkg',str(archive),'-p',loader,'-b','115200','--singlebank']
    result['command']=command
    print('Uploading verified merged application once via',loader,flush=True)
    with (OUT/'upload.txt').open('x') as log:
        process=subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,timeout=120)
    result['exit_code']=process.returncode
    assert process.returncode==0,'Upload failed; diagnose before any further write'
    result['uploaded']=True;result['application_port']=wait(0x8044,30)
    print('Upload succeeded; application reconnected on',result['application_port'],flush=True)
finally:
    (OUT/'upload-result.json').write_text(json.dumps(result,indent=2)+'\n')
