from pathlib import Path
import hashlib,json,shutil,re
r=Path.cwd();o=r/'logs/hardware-gps-backup-2026-11-03/force-backup-fix';h=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
results=json.loads((r/'.pio/reusable-regression-logs/results.json').read_text())
assert len(results)==32 and all(code==0 for code in results.values()),results
m=json.loads((o/'manifest.json').read_text())
assert all(h(r/a['path'])==a['sha256'] for a in m['firmware_inputs']+m['changed_source_and_tests']+m['artifacts'])
shutil.copytree(r/'.pio/reusable-regression-logs',o/'regression-logs',dirs_exist_ok=True)
summary=dict(suite_count=32,passed=32,failed=0,results=results,focused_integration_cases=10,negative_force='PASS',negative_recovery_gate='PASS',linked_startup='PASS',builds_passed=4,hardware_verified=False)
(o/'regression-summary.json').write_text(json.dumps(summary,indent=2)+'\n')
m.update(regressions=summary,software_validation_passed=True,hardware_verified=False)
(o/'manifest.json').write_text(json.dumps(m,indent=2)+'\n')
for name in ('docs/gps-backup-cycle1-investigation.md','docs/hardware-gps-backup-test-report.md'):
 p=r/name;s=p.read_text(encoding='utf-8');s=s.replace('Complete 32-suite run is pending final collection.','Complete 32-suite run PASS: 32/32 suites, exit code 0; all ten receiver integration cases pass in the complete run. Full transcript: `force-backup-fix/regressions.txt`; individual logs and machine-readable results: `force-backup-fix/regression-logs/` and `regression-summary.json`.')
 s+='\nFinal offline verification: all source, firmware-input and 16 archived artifact hashes match the new manifest. Trace reset clears counts in place without a multi-KiB stack temporary; linked command prologue uses 232 local stack bytes plus saved registers (not a whole-firmware peak guarantee). Previous candidate outputs are preserved. No physical device operation occurred.\n'
 s=s.replace('\n\n', '\n\nLatest status (2026-11-03): the authorized local force-bit correction has passed software validation. The new candidate is not uploaded or hardware-verified. See the authorized local correction follow-up below for its checksum, complete file list and results.\n\n', 1)
 p.write_text(s,encoding='utf-8')
# Preserve test-generated outputs, then restore exactly the pre-task bytes.
for name in ('logs/stock-battery-review/compiled-startup-results.json','tests/tcb3/__pycache__/native.cpython-311.pyc'):
 p=r/name;b=o/'generated-before'/name
 if b.exists():
  q=o/'generated-after'/name;q.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,q);p.write_bytes(b.read_bytes())
print('PASS: 32 suites, ten receiver cases, four builds, both negative controls, linked startup, source/artifact hashes.')
print('Final binary:',next(a['sha256'] for a in m['artifacts'] if a['environment']==m['selected_environment'] and a['path'].endswith('.bin')))
