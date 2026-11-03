from pathlib import Path
import hashlib,json,shutil,subprocess,zipfile
r=Path.cwd();o=r/'logs/hardware-gps-backup-2026-11-03/rx-event-diagnostic'
files=['platformio.ini','src/gps_power.h','src/gps_receiver.cpp','src/gps_backup_trace.h','src/gps_backup_uart_diag.h','src/gps_backup_uart_diag.cpp','tests/flash_write/Arduino.h','tests/reusable/gps_backup_test.cpp','tests/reusable/gps_backup_integration.cpp','tests/reusable/gps_backup_tests.py']

def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
source=[]
for name in files:
 p=r/name; q=o/'source'/name;q.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,q)
 source.append(dict(path=name,sha256=digest(p)))
artifacts=[]
envs=('seeed_xiao_nrf52840_sense','tcb3_first_write','tcb3_append_30s','tcb3_movement_120s','tacticpod_reusable_usb','tacticpod_reusable_usb_gpsbackup','tacticpod_reusable_usb_gpsbackup_auto')
for env in envs:
 d=o/env;d.mkdir(exist_ok=True);b=r/'.pio/build'/env
 for ext in ('elf','hex','zip'):shutil.copy2(b/('firmware.'+ext),d/('firmware.'+ext))
 with zipfile.ZipFile(d/'firmware.zip') as z:
  pkg=json.loads(z.read('manifest.json'))['manifest'];assert set(pkg)=={'application','dfu_version'}
  app=pkg['application'];assert app['init_packet_data']['softdevice_req']==[0x123]
  (d/'firmware.bin').write_bytes(z.read(app['bin_file']))
 for ext in ('elf','hex','zip','bin'):
  p=d/('firmware.'+ext);artifacts.append(dict(environment=env,path=p.relative_to(r).as_posix(),bytes=p.stat().st_size,sha256=digest(p)))
inputs=[]
for folder in ('src','lib','variants'):
 for p in sorted((r/folder).rglob('*')):
  if p.is_file() and '.git' not in p.parts:inputs.append(dict(path=p.relative_to(r).as_posix(),sha256=digest(p)))
inputs.append(dict(path='platformio.ini',sha256=digest(r/'platformio.ini')))
m=dict(schema_version=1,purpose='Test-only RX-event snapshots and sticky-failure receive-only observation',base_commit=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),source_is_uncommitted=True,changed_source_and_tests=source,firmware_inputs=inputs,artifacts=artifacts,selected_environment='tacticpod_reusable_usb_gpsbackup',automatic_backup_enabled_in_selected_environment=False,automatic_environment_compile_check_only=True,device_flashed=False,hardware_verified=False,installation_authorized=False,regressions='pending',gps_baud_recovery='38400 only; no new baud scan or persistent receiver configuration',observation_limits='Microsecond sequential register snapshots and host-read byte timestamps; no on-wire capture or definitive byte-origin proof')
(o/'manifest.json').write_text(json.dumps(m,indent=2)+'\n')
(o/'SHA256SUMS.txt').write_text(''.join(a['sha256']+'  '+a['path']+'\n' for a in artifacts))
print(next(a for a in artifacts if a['environment']==m['selected_environment'] and a['path'].endswith('.bin')))
print('Candidate archive complete; regression verification pending.')
