from pathlib import Path
import json,zipfile,hashlib,shutil,subprocess,io,ast
r=Path.cwd();o=r.parent/'logs/pr5-field-candidate-20261103';sha='3d4693207d459b1565af2e8b3955cd897f5d519c';h=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
artifacts=[]
for env in ('seeed_xiao_nrf52840_sense','tacticpod_reusable_usb'):
 b=r/'.pio/build'/env;d=o/env;d.mkdir(exist_ok=True)
 for ext in ('elf','hex','zip'):shutil.copy2(b/('firmware.'+ext),d/('firmware.'+ext))
 with zipfile.ZipFile(d/'firmware.zip') as z:
  pkg=json.loads(z.read('manifest.json'))['manifest'];assert set(pkg)=={'application','dfu_version'}
  app=pkg['application'];assert app['init_packet_data']['softdevice_req']==[0x123]
  (d/'firmware.bin').write_bytes(z.read(app['bin_file']))
 temp=d/'from-elf.bin'
 subprocess.run([str(r/'.pio-core/packages/toolchain-gccarmnoneeabi/bin/arm-none-eabi-objcopy.exe'),'-O','binary',str(d/'firmware.elf'),str(temp)],check=True)
 assert temp.read_bytes()==(d/'firmware.bin').read_bytes()
 for ext in ('elf','hex','zip','bin'):
  p=d/('firmware.'+ext);artifacts.append(dict(environment=env,path=p.relative_to(o).as_posix(),bytes=p.stat().st_size,sha256=h(p)))
inputs=[]
for folder in ('src','lib','variants'):
 for p in sorted((r/folder).rglob('*')):
  if p.is_file() and '.git' not in p.parts:inputs.append(dict(path=p.relative_to(r).as_posix(),sha256=h(p)))
inputs.append(dict(path='platformio.ini',sha256=h(r/'platformio.ini')))
source=subprocess.check_output(['git','-c','core.autocrlf=false','archive','--format=zip',sha]);(o/'source-3d46932.zip').write_bytes(source)
# Proven storage compatibility is based on exact committed source bytes.
paths=['src/tcb3_store.cpp','src/tcb3_store.h','src/tcb3_session.cpp','src/tcb3_test_limits.h','src/compact_recorder.cpp','src/compact_recorder.h','src/recorder.cpp','src/recorder_format.h','src/gps_receiver.cpp','src/gps_profile.cpp','src/gps_config.cpp','src/gps_rate.cpp','src/physical_button.h','tools/tcb3_decode.py','tools/tcb3_download.py']
for name in paths:assert subprocess.check_output(['git','show','155a999:'+name])==(r/name).read_bytes(),name
m=dict(schema_version=1,commit=sha,parent_commit='155a999122f0ce85ab6817c6eba690e9501fe4b3',environment='tacticpod_reusable_usb',source_is_uncommitted=False,source_archive_sha256=hashlib.sha256(source).hexdigest(),worktree=str(r),experimental_gps_backup_absent=True,automatic_gps_backup=False,recording_format='TCB3 3.2.0; TPX3 export unchanged',unchanged_compatibility_files=paths,project_input_hashes=inputs,artifacts=artifacts,application_only=True,softdevice_requirement='0x0123',bootloader_update=False,external_spi_erase_intended=False,hardware_tested=False,uploaded=False,installation_authorized=False,regressions='pending',platformio_core='6.2.0',seeed_platform_commit='a6fd41debbbcda13b5340fff50c9092ed3289194',library_commits=dict(TinyGPSPlus='ca29434514a5c5172bd807af0608df7f296582a2',Seeed_Arduino_LSM6DS3='e063c3a544b8bb89238accddf24f0028bcd1ca20'))
(o/'manifest.json').write_text(json.dumps(m,indent=2)+'\n');(o/'SHA256SUMS.txt').write_text(''.join(a['sha256']+'  '+a['path']+'\n' for a in artifacts))
print('PASS: source identity, application-only ZIP/BIN/ELF match, 15 unchanged compatibility files.')
for a in artifacts:
 if a['environment']=='tacticpod_reusable_usb' and a['path'].endswith(('.bin','.zip')):print(a)
