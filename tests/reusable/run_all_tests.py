"""All reusable/default regressions; host simulations only, no serial devices."""
from pathlib import Path
import subprocess,sys,json
root=Path(__file__).resolve().parents[2]
out=root/'.pio/reusable-regression-logs';out.mkdir(parents=True,exist_ok=True)
paths=['tests/gps/run_tests.py', 'tests/recording/run_tests.py', 'tests/flash_write/run_tests.py', 'tests/recorder_v2/run_tests.py', 'tests/storage_benchmark/run_tests.py', 'tests/compact/run_tests.py', 'tests/compact/test_decoder.py', 'tests/compact/test_timing_model.py', 'tests/compact/test_resources.py', 'tests/tcb3/run_tests.py', 'tests/tcb3/integration_tests.py', 'tests/tcb3/console_tests.py', 'tests/flash_identity/run_tests.py', 'tests/tcb3/sustained_tests.py', 'tests/tcb3/long_integration_tests.py', 'tests/recording/erase_review_tests.py', 'tests/tcb3/first_write_tests.py', 'tests/tcb3/append_30s_tests.py', 'tests/tcb3/movement_120s_tests.py', 'tests/tcb3/movement_controls_tests.py', 'tests/reusable/policy_tests.py', 'tests/reusable/store_tests.py', 'tests/reusable/reset_tests.py', 'tests/reusable/adc_tests.py', 'tests/reusable/console_tests.py', 'tests/reusable/integration_tests.py']
paths.append('tests/reusable/startup_tests.py')
paths.extend(['tests/reusable/switchless_tests.py','tests/reusable/download_tests.py','tests/reusable/button_tests.py','tests/reusable/standby_tests.py','tests/reusable/battery_gauge_tests.py'])
paths.append('tests/reusable/field_tests.py')
results={}
for path in paths:
    name=path.replace('/','_').replace('.py','')
    print('RUN',path,flush=True)
    with (out/(name+'.txt')).open('w') as f:
        p=subprocess.run([sys.executable,path],cwd=root,stdout=f,stderr=subprocess.STDOUT)
    results[path]=p.returncode
    (out/'results.json').write_text(json.dumps(results,indent=2)+'\n')
    print('PASS' if p.returncode==0 else 'FAIL',path,flush=True)
sys.exit(0 if all(v==0 for v in results.values()) else 1)
