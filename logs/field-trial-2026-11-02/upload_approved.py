"""One explicitly approved application-only DFU upload. No retries."""
from pathlib import Path
import hashlib, json, subprocess, sys, time, zipfile
from serial.tools import list_ports

ROOT = Path(__file__).resolve().parents[2]
OUT = Path(__file__).resolve().parent
EXPECTED = 'd90d678ed1840a123756b796bb6a4288df5239d32e0399ed3255b4b831d310ff'
BIN_HASH = 'b2ea6b3bab286b97ecc4235039944d1be335d07711c579952fe99dea120a4ddc'
SERIAL = '0E8A89B7289430BD'
archive = OUT/'firmware.zip'
result = {'uploaded': False, 'recording_started': False, 'calibration_changed': False,
          'application_only': True, 'zip_sha256': EXPECTED, 'bin_sha256': BIN_HASH}
assert hashlib.sha256(archive.read_bytes()).hexdigest() == EXPECTED
with zipfile.ZipFile(archive) as z:
    manifest = json.loads(z.read('manifest.json'))['manifest']
    assert set(manifest) == {'application', 'dfu_version'}
    assert hashlib.sha256(z.read(manifest['application']['bin_file'])).hexdigest() == BIN_HASH
    assert manifest['application']['init_packet_data']['softdevice_req'] == [0x123]
pre = json.loads((OUT/'preservation-result.json').read_text())
assert pre['passed'] and pre['serial_released']
assert hashlib.sha256((OUT/'before-upload.tpx3').read_bytes()).hexdigest() == pre['archive']['sha256']
ports = [p for p in list_ports.comports() if p.device == 'COM7' and p.vid == 0x2886
         and p.pid == 0x0045 and p.serial_number == SERIAL]
assert len(ports) == 1, 'Approved bootloader identity changed; no upload'
assert Path('E:/INFO_UF2.TXT').read_bytes() == (OUT/'INFO_UF2.TXT').read_bytes()
command = [sys.executable, str(ROOT/'.pio-core/packages/tool-adafruit-nrfutil/adafruit-nrfutil.py'),
           'dfu', 'serial', '-pkg', str(archive), '-p', 'COM7', '-b', '115200', '--singlebank']
result['command'] = command
with (OUT/'upload-attempted.json').open('x') as f:
    json.dump(dict(result, approval='User explicitly approved Phase 4 via DFU COM7; no recording or calibration'), f, indent=2)
try:
    print('Verified approved ZIP/BIN hashes, application-only manifest, backup and COM7 identity.', flush=True)
    with (OUT/'upload.txt').open('x') as log:
        process = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, timeout=120)
    result['exit_code'] = process.returncode
    assert process.returncode == 0, 'Upload failed; stop and diagnose, no retry'
    result['uploaded'] = True
    end = time.monotonic()+30
    while time.monotonic() < end:
        apps = [p for p in list_ports.comports() if p.vid == 0x2886 and p.pid == 0x8044 and p.serial_number == SERIAL]
        if len(apps) == 1:
            result['application_port'] = apps[0].device
            break
        time.sleep(.1)
    assert 'application_port' in result, 'Upload completed but application did not enumerate'
    print('Application DFU succeeded; reconnected on '+result['application_port'], flush=True)
finally:
    (OUT/'upload-result.json').write_text(json.dumps(result, indent=2)+'\n')
