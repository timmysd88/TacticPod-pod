import pathlib,json,subprocess,sys
root=pathlib.Path.cwd();out=root/'logs/phase3b10-review'
suites=json.loads((root/'logs/phase3b8-review/regressions.json').read_text());results={}
movement_command=[sys.executable,'tests/tcb3/movement_120s_tests.py']
movement_log=(out/'movement-120s.txt').open('w')
movement=subprocess.Popen(movement_command,stdout=movement_log,stderr=subprocess.STDOUT,cwd=root)
for name,item in suites.items():
    print('RUN',name,flush=True)
    command=[sys.executable,*item['command'][1:]]
    with (out/(name+'.txt')).open('w') as f:r=subprocess.run(command,stdout=f,stderr=subprocess.STDOUT,cwd=root)
    results[name]={'command':command,'exit_code':r.returncode}
    (out/'regressions.json').write_text(json.dumps(results,indent=2)+'\n')
    print('RESULT',name,r.returncode,flush=True)
results['movement-120s']={'command':movement_command,'exit_code':movement.wait()}
movement_log.close()
(out/'regressions.json').write_text(json.dumps(results,indent=2)+'\n')
controls_command=[sys.executable,'tests/tcb3/movement_controls_tests.py']
with (out/'movement-controls.txt').open('w') as f:
    controls=subprocess.run(controls_command,stdout=f,stderr=subprocess.STDOUT,cwd=root)
results['movement-controls']={'command':controls_command,'exit_code':controls.returncode}
(out/'regressions.json').write_text(json.dumps(results,indent=2)+'\n')
print('ALL PASSED' if all(v['exit_code']==0 for v in results.values()) else 'FAILURES PRESENT',flush=True)
