"""Continue the one approved upload after loader entry, without another reset."""
from pathlib import Path
import hashlib
import importlib.util
import json
import subprocess
import sys
import zipfile

root = Path(__file__).resolve().parents[2]
out = Path(__file__).with_name('hardware-run-approved-20261103')
approved = 'aced1874981173ebfbb3904390f6088214aca3419f72f6a5fae0deb4f4c14051'
spec = importlib.util.spec_from_file_location('hw', Path(__file__).with_name('hardware_run.py'))
hw = importlib.util.module_from_spec(spec)
spec.loader.exec_module(hw)
hw.OUT = out
result = json.loads((out/'upload-result.json').read_text())
assert not result['uploaded'] and 'command' not in result
assert not (out/'upload.txt').exists(), 'A DFU process was already invoked; never repeat it'
pre = json.loads((out/'preflight-result.json').read_text())
assert pre['passed'] and pre['serial_released']
candidate = pre['candidate']
assert candidate['binary_sha256'] == approved
manifest = json.loads((Path(__file__).parent/'recovery-gate-fix/manifest.json').read_text())
for a in manifest['changed_source_and_tests'] + manifest['artifacts']:
    assert hashlib.sha256((root/a['path']).read_bytes()).hexdigest() == a['sha256'], a['path']
package = Path(candidate['package'])
assert hashlib.sha256(package.read_bytes()).hexdigest() == candidate['zip_sha256']
with zipfile.ZipFile(package) as z:
    m = json.loads(z.read('manifest.json'))['manifest']
    assert set(m) == {'application', 'dfu_version'}
    assert m['application']['init_packet_data']['softdevice_req'] == [0x123]
    assert hashlib.sha256(z.read(m['application']['bin_file'])).hexdigest() == approved
loader = hw.identify(0x0045)
assert loader.device == result['loader_port'] == 'COM7'
assert loader.serial_number == hw.NUMBER
result.update(loader_identity_verified_by='Same unique device serial as approved COM8, expected 2886:0045 serial DFU bootloader, recorded Sense identity; no new reset',
              uf2_disk_exposed=False, fresh_info_uf2_read=False,
              approved_binary_sha256=approved, no_firmware_bytes_sent_by_initial_helper=True,
              application_only=True, external_spi_erase_intended=False)
command = [sys.executable, str(root/'.pio-core/packages/tool-adafruit-nrfutil/adafruit-nrfutil.py'),
           'dfu','serial','-pkg',str(package),'-p',loader.device,'-b','115200','--singlebank']
result['command'] = command
with (out/'dfu-invoked-once.json').open('x') as f:
    json.dump(result, f, indent=2)
try:
    print('Uploading approved application once through verified serial DFU on', loader.device, flush=True)
    with (out/'upload.txt').open('x') as log:
        process = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, timeout=120)
    result['exit_code'] = process.returncode
    assert process.returncode == 0, 'DFU failed; stop without another upload'
    result['uploaded'] = True
    result['application_port'] = hw.identify(0x8044, 30).device
    print('Upload PASS; application returned on', result['application_port'], flush=True)
finally:
    hw.save('upload-result.json', result)
