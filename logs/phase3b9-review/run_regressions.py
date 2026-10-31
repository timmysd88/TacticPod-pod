import pathlib,json,subprocess,sys
root=pathlib.Path.cwd();out=root/'logs/phase3b9-review'
suites=json.loads((root/'logs/phase3b8-review/regressions.json').read_text());results={}
for name,item in suites.items():
    print('RUN',name,flush=True)
    command=[sys.executable,*item['command'][1:]]
    with (out/(name+'.txt')).open('w') as f:r=subprocess.run(command,stdout=f,stderr=subprocess.STDOUT,cwd=root)
    results[name]={'command':command,'exit_code':r.returncode}
    (out/'regressions.json').write_text(json.dumps(results,indent=2)+'\n')
    print('RESULT',name,r.returncode,flush=True)
print('ALL PASSED' if all(v['exit_code']==0 for v in results.values()) else 'FAILURES PRESENT',flush=True)
