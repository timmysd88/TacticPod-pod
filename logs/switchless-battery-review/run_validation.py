"""Offline switchless validation; no device enumeration, upload or serial access."""
from pathlib import Path
import subprocess,sys,json
ROOT=Path(__file__).resolve().parents[2];OUT=Path(__file__).resolve().parent
paths=['tests/reusable/switchless_tests.py','tests/reusable/download_tests.py',
    'tests/reusable/startup_tests.py','tests/reusable/compiled_startup_tests.py',
    'tests/reusable/policy_tests.py','tests/reusable/adc_tests.py',
    'tests/reusable/console_tests.py','tests/reusable/integration_tests.py',
    'logs/stock-battery-review/audit_sources.py']
results={}
for path in paths:
    print('RUN',path,flush=True)
    with (OUT/(Path(path).stem+'.txt')).open('w') as f:
        p=subprocess.run([sys.executable,path],cwd=ROOT,stdout=f,stderr=subprocess.STDOUT)
    results[path]=p.returncode
    (OUT/'regressions.json').write_text(json.dumps(results,indent=2)+'\n')
    print('PASS' if p.returncode==0 else 'FAIL',path,flush=True)
sys.exit(0 if all(c==0 for c in results.values()) else 1)
