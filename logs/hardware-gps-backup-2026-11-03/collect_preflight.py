"""Collect local artifacts/provenance only. Never opens a device or uploads."""
from pathlib import Path
import hashlib
import json
import subprocess
import zipfile

root = Path(__file__).resolve().parents[2]
out = Path(__file__).resolve().parent
sha = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip()
assert sha == '29f90966d868f68b85d63d64d27bab9584f7628a'
env = 'tacticpod_reusable_usb_gpsbackup'
build = root / '.pio/build' / env
with zipfile.ZipFile(build / 'firmware.zip') as archive:
    package = json.loads(archive.read('manifest.json'))['manifest']
    assert set(package) == {'application', 'dfu_version'}
    app = package['application']
    assert app['init_packet_data']['softdevice_req'] == [0x123]
    binary = archive.read(app['bin_file'])
    (out / 'gps-backup-test-application.bin').write_bytes(binary)

files = [build / ('firmware.' + ext) for ext in ('zip', 'hex', 'elf')]
files.append(out / 'gps-backup-test-application.bin')
artifacts = [dict(path=str(p.relative_to(root)), bytes=p.stat().st_size,
                  sha256=hashlib.sha256(p.read_bytes()).hexdigest()) for p in files]
manifest = dict(schema_version=1, artifact_kind='hardware-test-preflight',
    release_version=None, pilot_ready=False, installation_authorized=False,
    repository='timmysd88/TacticPod-pod', pr_number=6, pr_state_at_sync='open',
    source_commit=sha,
    branch=subprocess.check_output(['git','branch','--show-current'],cwd=root,text=True).strip(),
    environment=env, platformio_core='6.2.0',
    seeed_platform_commit='a6fd41debbbcda13b5340fff50c9092ed3289194',
    library_commits=dict(LSM6DS3='e063c3a544b8bb89238accddf24f0028bcd1ca20',
                         TinyGPSPlus='ca29434514a5c5172bd807af0608df7f296582a2'),
    build_flags=['-DTARGET_SEEED_XIAO_NRF52840_SENSE','-DTACTICPOD_TCB3_FLASH_ENABLED=1',
                 '-DTACTICPOD_REUSABLE=1','-DTACTICPOD_GPS_BACKUP_TEST=1'],
    automatic_gps_backup=False, usb_monitor_baud=115200, gps_uart_baud=38400,
    enumerated_device=dict(port='COM8',vid='2886',pid='8044',serial='0E8A89B7289430BD'),
    bootloader_currently_verified=False, historical_bootloader='UF2 0.6.1; S140 7.3.0 (2026-11-02 capture)',
    upload=dict(method='Adafruit nrfutil serial DFU',application_only=True,
                baud=115200,singlebank=True,package='.pio/build/'+env+'/firmware.zip'),
    source_blocker='Circular profile gate prevents GPS BACKUP TEST from passing',
    hardware_cycles=[], artifacts=artifacts)
(out / 'preflight-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
(out / 'SHA256SUMS.txt').write_text(''.join(a['sha256']+'  '+a['path']+'\n' for a in artifacts))
print(json.dumps(manifest,indent=2))
