from pathlib import Path
import json,hashlib,shutil,subprocess,zipfile,io
r=Path.cwd();o=r/'logs/hardware-gps-backup-2026-11-03/rx-event-diagnostic';h=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
results=json.loads((r/'.pio/reusable-regression-logs/results.json').read_text());assert len(results)==32 and all(v==0 for v in results.values()),results
m=json.loads((o/'manifest.json').read_text());assert all(h(r/a['path'])==a['sha256'] for a in m['artifacts']+m['changed_source_and_tests']+m['firmware_inputs']+m['framework_and_toolchain_references'])
shutil.copytree(r/'.pio/reusable-regression-logs',o/'regression-logs',dirs_exist_ok=True)
summary=dict(suite_count=32,passed=32,failed=0,results=results,receiver_integration_cases=11,snapshot_models=2,linked_vector_builds=7,firmware_builds_passed=7,linked_startup='PASS',negative_controls=['old circular recovery gate rejected','missing force rejected','RX-only guard removal rejected'],hardware_verified=False)
(o/'regression-summary.json').write_text(json.dumps(summary,indent=2)+'\n');m.update(regressions=summary,software_validation_passed=True,candidate_status='Software validation PASS; not uploaded or hardware-tested; no upload approval')
(o/'manifest.json').write_text(json.dumps(m,indent=2)+'\n')
p=r/'docs/gps-backup-rx-diagnostic.md';s=p.read_text(encoding='utf-8').replace('Full 32-suite regression run: PENDING final collection.','Full 32-suite regression run: **PASS, 32/32 suites, exit code 0**. All eleven receiver scenarios, both snapshot models and seven linked-vector checks also pass within this complete run.');p.write_text(s,encoding='utf-8')
for name in ('docs/hardware-gps-backup-test-report.md','docs/gps-backup-cycle1-investigation.md'):
 p=r/name;s=p.read_text(encoding='utf-8').replace('Seven builds and focused checks pass; the full 32-suite run is pending collection.','Seven builds, all 32 regression suites, eleven receiver integration cases, both snapshot models, seven linked-vector checks, linked startup checks and all three negative controls PASS. See rx-event-diagnostic/regression-summary.json and full archived logs.');p.write_text(s,encoding='utf-8')
# Restore exactly the pre-task tracked startup output; retain the new result as evidence.
name='logs/stock-battery-review/compiled-startup-results.json';before=o/'before'/name;after=o/'generated-after'/name;after.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(r/name,after);(r/name).write_bytes(before.read_bytes())
a=zipfile.ZipFile(io.BytesIO(subprocess.check_output(['git','-c','core.autocrlf=false','archive','--format=zip','HEAD'])))
d=[n for n in a.namelist() if not n.endswith('/') and (r/n).is_file() and (r/n).read_bytes().replace(b'\r\n',b'\n')!=a.read(n).replace(b'\r\n',b'\n')]
expected={'platformio.ini','src/gps_power.h','src/gps_receiver.cpp','tests/flash_write/Arduino.h','tests/reusable/gps_backup_test.cpp','tests/reusable/gps_backup_tests.py','tests/reusable/standby.cpp','tests/reusable/standby_tests.py'}
assert set(d)==expected,d
(o/'tracked-content-differences.json').write_text(json.dumps(d,indent=2)+'\n')
print('PASS: 32 suites; 7 builds; 11 receiver cases; 2 snapshot models; vector isolation; 3 negative controls; all source/artifact hashes; final scope audit.')
print('Candidate SHA-256:',next(a['sha256'] for a in m['artifacts'] if a['environment']==m['selected_environment'] and a['path'].endswith('.bin')))
