"""Authorized application ZIP upload and read-only flash identity capture.

No external-flash mutation command is in the host command allowlist.
"""
import hashlib,json,pathlib,re,subprocess,sys,time,zipfile
import serial
from serial.tools import list_ports

ROOT=pathlib.Path(__file__).resolve().parents[2]
OUT=pathlib.Path(__file__).resolve().parent
SERIAL='0E8A89B7289430BD'
ALLOWED={'RECORD STATUS','GPS STATUS','OBS STATUS','COMP STATUS','TCB STATUS','DIAG ID'}
result={'commands':[],'upload':False,'serial_released':False,'external_flash_mutation_commands_sent':0}
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def devices():return [p for p in list_ports.comports() if p.serial_number==SERIAL and p.vid==0x2886]
def wait_port(pid,timeout):
    end=time.monotonic()+timeout
    while time.monotonic()<end:
        for p in devices():
            if p.pid==pid:return p.device
        time.sleep(.05)
    raise RuntimeError('USB enumeration timed out for '+hex(pid))
def receive(p,log,seconds,marker=None):
    end=time.monotonic()+seconds;data=bytearray()
    while time.monotonic()<end:
        chunk=p.read(max(1,p.in_waiting))
        if chunk:log.write(chunk);log.flush();data.extend(chunk)
        if marker and marker in data:
            index=data.index(marker)
            if b'\n' in data[index:]:return data.decode('utf-8','replace')
    if marker:raise RuntimeError('response timeout: '+marker.decode())
    return data.decode('utf-8','replace')
def command(p,log,text,marker,timeout=5):
    assert text in ALLOWED
    result['commands'].append(text)
    log.write(('\nHOST TX '+text+'\n').encode());log.flush()
    p.write((text+'\n').encode());p.flush()
    data=receive(p,log,timeout,marker.encode())
    if any(x in data for x in ('HardFault','HARDFAULT','ASSERT FAILED','FLASH ID ERROR')):
        raise RuntimeError('device fault/error: '+data)
    return data
def stopped(p,log):
    reports={}
    for cmd,marker in [('RECORD STATUS','RECORD STATUS:'),('OBS STATUS','OBS END'),('COMP STATUS','COMP END'),('TCB STATUS','TCB DISABLED')]:
        reports[cmd]=command(p,log,cmd,marker)
    assert 'RECORD STATUS: STOPPED' in reports['RECORD STATUS'],reports
    assert 'capture=OFF' in reports['OBS STATUS'] and 'RAM_fast_imu=OFF' in reports['OBS STATUS'],reports
    assert 'capturing=0' in reports['COMP STATUS'] and 'fault=0' in reports['COMP STATUS'],reports
    return reports
try:
    artifact=ROOT/'.pio/build/seeed_xiao_nrf52840_sense/firmware.elf'
    evidence=json.loads((ROOT/'benchmarks/flash-identification/evidence.json').read_text())
    assert digest(artifact)==evidence['firmware_elf_sha256'],'ELF differs from tested build'
    package=artifact.with_suffix('.zip');binary=OUT/'tested-application.bin'
    objcopy=ROOT/'.pio-core/packages/toolchain-gccarmnoneeabi/bin/arm-none-eabi-objcopy.exe'
    subprocess.check_call([str(objcopy),'-O','binary',str(artifact),str(binary)])
    with zipfile.ZipFile(package) as z:
        assert z.read('firmware.bin')==binary.read_bytes(),'ZIP payload mismatch'
        manifest=json.loads(z.read('manifest.json'))
        assert set(manifest['manifest'])=={'application','dfu_version'},'must be application-only DFU'
    raw=binary.read_bytes()
    assert b'DIAG ID' in raw and b'BEGIN_FLASH_ID' in raw and b'TCB DISABLED:' in raw
    assert b'TCB START scanning; no auto resume' not in raw
    # The linked production ELF must not contain enabled store/session objects.
    nm=ROOT/'.pio-core/packages/toolchain-gccarmnoneeabi/bin/arm-none-eabi-nm.exe'
    symbols=subprocess.check_output([str(nm),'-C',str(artifact)],text=True)
    assert 'TcbDetail::store' not in symbols and 'Tcb3::Store::' not in symbols
    result['preflight']={'elf_sha256':digest(artifact),'bin_sha256':digest(binary),'zip_sha256':digest(package),
        'tcb3_enabled':False,'diag_id_present':True,'application_only_zip':True,'usb_serial_number':SERIAL}
    (OUT/'preflight.json').write_text(json.dumps(result['preflight'],indent=2)+'\n')
    loaders=[p for p in devices() if p.pid==0x0045]
    if not loaders:
        app=wait_port(0x8044,3)
        with serial.Serial(app,1200,timeout=.1) as touch:touch.dtr=False
    loader=wait_port(0x0045,15)
    print('Bootloader:',loader,flush=True)
    cmd=[sys.executable,str(ROOT/'.pio-core/packages/tool-adafruit-nrfutil/adafruit-nrfutil.py'),
         'dfu','serial','-pkg',str(package),'-p',loader,'-b','115200','--singlebank']
    with (OUT/'upload.txt').open('w') as log:
        completed=subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,timeout=90)
    assert completed.returncode==0,'DFU failed; see upload.txt'
    result['upload']=True;print('Application ZIP uploaded',flush=True)
    app=wait_port(0x8044,20)
    with serial.Serial(app,115200,timeout=.03,write_timeout=1) as p,(OUT/'serial-transcript.txt').open('wb') as log:
        startup=receive(p,log,22)
        (OUT/'startup.txt').write_text(startup,encoding='utf-8')
        result['startup']=startup
        for line in startup.splitlines():
            if any(s in line for s in ('GPS PROFILE','GPS STARTUP','JEDEC','SFDP BFPT','scan complete','ERROR','FAULT')):print(line,flush=True)
        assert 'RECORD scan complete' in startup,'startup scan not confirmed'
        assert 'ERROR:' not in startup and 'FAULT' not in startup,'startup fault'
        result['before']=stopped(p,log)
        gps=command(p,log,'GPS STATUS','GPS speed_',5)
        result['gps_before']=gps
        assert '10HZ VERIFIED RAM' in gps,'GPS automatic profile not verified'
        for capture in (1,2):
            data=command(p,log,'DIAG ID','END_FLASH_ID',10)
            assert 'BEGIN_FLASH_ID' in data and 'JEDEC_repeat=PASS' in data and 'SFDP_repeat=PASS' in data,data
            (OUT/f'diag-id-{capture}.txt').write_text(data,encoding='utf-8')
            print('DIAG ID capture',capture,'complete',flush=True)
        result['after']=stopped(p,log)
        result['gps_after']=command(p,log,'GPS STATUS','GPS speed_',5)
        assert '10HZ VERIFIED RAM' in result['gps_after']
        result['status']='complete'
    result['serial_released']=True
    print(app,'released; read-only captures complete',flush=True)
except BaseException as exc:
    result['status']='failed';result['error']=repr(exc)
    result['serial_released']=True # every serial handle is managed by a with block
    raise
finally:
    (OUT/'run-result.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
