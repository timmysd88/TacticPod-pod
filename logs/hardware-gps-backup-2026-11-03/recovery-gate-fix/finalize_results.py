"""Collect only a completed successful run; preserve and restore generated files."""
from pathlib import Path
import hashlib
import io
import json
import shutil
import subprocess
import zipfile

root = Path(__file__).resolve().parents[3]
out = Path(__file__).resolve().parent
results = json.loads((root / '.pio/reusable-regression-logs/results.json').read_text())
assert len(results) == 32 and all(code == 0 for code in results.values()), results
shutil.copytree(root / '.pio/reusable-regression-logs', out / 'regression-logs', dirs_exist_ok=True)
(out / 'regression-summary.json').write_text(json.dumps(dict(
    suite_count=32, passed=32, failed=0, runner_exit_code=0,
    special_linker_adapters=False, results=results,
    focused_integration_cases=7, negative_control='PASS', linked_startup='PASS',
    hardware_verified=False), indent=2) + '\n')

# Only these known test-generated files in this new isolated worktree are
# restored. The original main worktree and all source edits remain untouched.
for name in ('logs/stock-battery-review/compiled-startup-results.json',
             'tests/tcb3/__pycache__/native.cpython-311.pyc'):
    p = root / name
    saved = out / 'generated-outputs' / name
    saved.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(p, saved)
    p.write_bytes(subprocess.check_output(['git', 'show', 'HEAD:' + name], cwd=root))

archive = zipfile.ZipFile(io.BytesIO(subprocess.check_output(
    ['git', '-c', 'core.autocrlf=false', 'archive', '--format=zip', 'HEAD'], cwd=root)))
differences = [n for n in archive.namelist() if not n.endswith('/') and (root/n).is_file()
               and (root/n).read_bytes().replace(b'\r\n', b'\n') != archive.read(n).replace(b'\r\n', b'\n')]
assert set(differences) == {'src/gps_power.h', 'src/gps_receiver.cpp',
                          'tests/reusable/gps_backup_test.cpp', 'tests/reusable/gps_backup_tests.py'}, differences
(out/'tracked-content-differences.json').write_text(json.dumps(differences, indent=2)+'\n')

manifest_path = out / 'manifest.json'
manifest = json.loads(manifest_path.read_text())
assert all(hashlib.sha256((root/a['path']).read_bytes()).hexdigest() == a['sha256']
           for a in manifest['artifacts'])
assert all(hashlib.sha256((root/a['path']).read_bytes()).hexdigest() == a['sha256']
           for a in manifest['changed_source_and_tests'])
manifest.update(regression_results=results, software_ready_for_supervised_test=True,
                full_suite_exit_code=0, special_linker_adapters=False,
                negative_control='PASS', linked_startup='PASS')
manifest_path.write_text(json.dumps(manifest, indent=2)+'\n')

report = root / 'docs/hardware-gps-backup-test-report.md'
text = report.read_text(encoding='utf-8')
old = '**Complete regression run: PENDING final collection.** The full 32-suite runner was started on the corrected source. Results and logs will be recorded here when it finishes. The additional linked startup test already passed against the rebuilt reusable/default ELFs. These are host simulations/ARM emulation, never physical hardware verification.'
assert old in text
text = text.replace(old, '**Complete regression run: PASS — 32/32 suites, exit code 0.** The full runner completed directly on the corrected source with no special linker adapters or qualified reruns. The expanded GPS suite includes all seven receiver integration cases above. The additional linked startup test and negative control also passed. Results: `recovery-gate-fix/regression-summary.json`, full transcript `regressions.txt`, individual logs in `regression-logs/`. These are host simulations/ARM emulation, never physical hardware verification.')
text = text.replace('**Supervised hardware test:** software correction and focused integration/negative-control checks pass; final readiness awaits completion of the full regression collection below.', '**Supervised hardware test: READY at the software-validation level.** The correction, all 32 regression suites, seven recovery integration cases, negative control, four firmware builds and linked startup checks passed.')
text = text.replace('Synchronisation, focused recovery regression tests, negative control, four builds and linked startup verification.', 'Synchronisation, all 32 regression suites, seven recovery integration cases, negative control, four builds and linked startup verification.')
text += '\nGenerated startup/bytecode outputs were copied into `recovery-gate-fix/generated-outputs/` and their tracked files restored to the base commit within `.p6`. The final content audit confirms only the four intended tracked source/test files differ, plus the new integration test/report and evidence artifacts. Original `main` work remains preserved.\n'
report.write_text(text, encoding='utf-8')
print('PASS: 32/32 suites; hashes verified; generated outputs preserved; scope confirmed; report finalized')
