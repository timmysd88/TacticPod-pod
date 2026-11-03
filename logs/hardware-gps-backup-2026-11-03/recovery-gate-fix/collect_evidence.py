"""Record the uncommitted correction and build identity; never accesses hardware."""
from pathlib import Path
import difflib
import hashlib
import json
import subprocess
import zipfile

root = Path(__file__).resolve().parents[3]
out = Path(__file__).resolve().parent
base = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip()
files = ['src/gps_receiver.cpp', 'src/gps_power.h',
         'tests/reusable/gps_backup_test.cpp', 'tests/reusable/gps_backup_tests.py',
         'tests/reusable/gps_backup_integration.cpp']
patch = ''
source = []
for name in files:
    p = root / name
    original = subprocess.run(['git', 'show', 'HEAD:' + name], cwd=root, capture_output=True)
    before = original.stdout.decode() if original.returncode == 0 else ''
    after = p.read_text()
    patch += ''.join(difflib.unified_diff(before.splitlines(True), after.splitlines(True),
                    fromfile='a/' + name if before else '/dev/null', tofile='b/' + name))
    source.append(dict(path=name, sha256=hashlib.sha256(p.read_bytes()).hexdigest()))
(out / 'recovery-gate.patch').write_text(patch)
artifacts = []
for env in ('seeed_xiao_nrf52840_sense', 'tacticpod_reusable_usb',
            'tacticpod_reusable_usb_gpsbackup', 'tacticpod_reusable_usb_gpsbackup_auto'):
    build = root / '.pio/build' / env
    with zipfile.ZipFile(build / 'firmware.zip') as z:
        package = json.loads(z.read('manifest.json'))['manifest']
        assert set(package) == {'application', 'dfu_version'}
        app = package['application']
        assert app['init_packet_data']['softdevice_req'] == [0x123]
        binary = out / env / 'firmware.bin'
        binary.parent.mkdir(exist_ok=True)
        binary.write_bytes(z.read(app['bin_file']))
    for p in [build / ('firmware.' + ext) for ext in ('zip', 'hex', 'elf')] + [binary]:
        artifacts.append(dict(environment=env, path=str(p.relative_to(root)),
                              bytes=p.stat().st_size, sha256=hashlib.sha256(p.read_bytes()).hexdigest()))
manifest = dict(schema_version=1, purpose='supervised-hardware-test-candidate',
    repository='timmysd88/TacticPod-pod', base_commit=base, source_is_uncommitted=True,
    branch=subprocess.check_output(['git','branch','--show-current'],cwd=root,text=True).strip(),
    patch_sha256=hashlib.sha256((out/'recovery-gate.patch').read_bytes()).hexdigest(),
    changed_source_and_tests=source, artifacts=artifacts,
    selected_environment='tacticpod_reusable_usb_gpsbackup',
    selected_build_flags=['-DTARGET_SEEED_XIAO_NRF52840_SENSE','-DTACTICPOD_TCB3_FLASH_ENABLED=1',
                         '-DTACTICPOD_REUSABLE=1','-DTACTICPOD_GPS_BACKUP_TEST=1'],
    automatic_backup_enabled_in_selected_environment=False,
    automatic_environment_compile_check_only=True,
    device_flashed=False, hardware_verified=False, installation_authorized=False,
    pilot_ready=False, port_observed='COM8', usb_baud=115200, gps_uart_baud=38400)
results = root / '.pio/reusable-regression-logs/results.json'
if results.exists():
    manifest['regression_results'] = json.loads(results.read_text())
(out/'manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')
(out/'SHA256SUMS.txt').write_text(''.join(a['sha256']+'  '+a['path']+'\n' for a in artifacts))
print('Source patch and 16 build artifact hashes saved; base', base)
