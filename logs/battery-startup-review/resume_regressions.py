"""Resume the remaining offline suites after interruption; no hardware access."""
from pathlib import Path
import json,subprocess,sys
ROOT=Path(__file__).resolve().parents[2]
out=ROOT/'.pio/reusable-regression-logs'
results=json.loads((out/'results.json').read_text())
pending=['tests/tcb3/append_30s_tests.py','tests/tcb3/movement_120s_tests.py',
    'tests/tcb3/movement_controls_tests.py','tests/reusable/policy_tests.py',
    'tests/reusable/store_tests.py','tests/reusable/adc_tests.py',
    'tests/reusable/console_tests.py','tests/reusable/integration_tests.py',
    'tests/reusable/startup_tests.py']
assert len(results)==17 and all(v==0 for v in results.values()),results
for path in pending:
    print('RUN',path,flush=True)
    with (out/(path.replace('/','_').replace('.py','')+'.txt')).open('w') as f:
        p=subprocess.run([sys.executable,path],cwd=ROOT,stdout=f,stderr=subprocess.STDOUT)
    results[path]=p.returncode
    (out/'results.json').write_text(json.dumps(results,indent=2)+'\n')
    print('PASS' if p.returncode==0 else 'FAIL',path,flush=True)
assert len(results)==26
sys.exit(0 if all(v==0 for v in results.values()) else 1)
