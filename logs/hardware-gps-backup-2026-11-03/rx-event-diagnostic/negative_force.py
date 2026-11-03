from pathlib import Path
import subprocess,sys,json
r=Path.cwd();sys.path.insert(0,str(r/'tests/tcb3'));from native import compile,Native
cc=r/'.pio-core/packages/toolchain-gccarmnoneeabi/bin/arm-none-eabi-g++.exe'
s=subprocess.check_output([str(cc),'-E','-std=c++11','-mcpu=cortex-m4','-mthumb','tests/reusable/gps_backup_test.cpp'])
old=b'const uint32_t flags = kFlagBackup | kFlagForce;';assert s.count(old)==1
p=r/'.pio/backup-missing-force.ii';p.write_bytes(s.replace(old,b'const uint32_t flags = kFlagBackup;'))
elf=compile('.pio/backup-missing-force.ii','backup-missing-force',extra=('-Wl,-e,main',))
code=Native(elf).call('main');assert code==161,code
result=dict(result='PASS',missing_force_rejected=True,expected_failure_code=161,observed=code,production_sources_modified=False)
(r/'logs/hardware-gps-backup-2026-11-03/rx-event-diagnostic/negative-force.json').write_text(json.dumps(result,indent=2)+'\n')
print(result)
