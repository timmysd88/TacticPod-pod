import subprocess,sys,pathlib,json
out=pathlib.Path('logs/reusable-review');r={}
for name,path in [('battery-policy','tests/reusable/policy_tests.py'),('reusable-store','tests/reusable/store_tests.py'),('battery-adc','tests/reusable/adc_tests.py'),('reusable-console','tests/reusable/console_tests.py'),('reusable-integration','tests/reusable/integration_tests.py')]:
 with (out/(name+'.txt')).open('w') as f:p=subprocess.run([sys.executable,path],stdout=f,stderr=subprocess.STDOUT)
 r[name]={'command':[sys.executable,path],'exit_code':p.returncode};(out/'new-regressions.json').write_text(json.dumps(r,indent=2)+'\n');print(name,p.returncode,flush=True)
