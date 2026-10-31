from pathlib import Path
import json,subprocess,sys
root=Path.cwd();out=root/'logs/reusable-review';suites=json.loads((root/'logs/phase3b10-review/regressions.json').read_text());r={}
for name,item in suites.items():
 print('RUN',name,flush=True);cmd=[sys.executable,*item['command'][1:]]
 with (out/(name+'.txt')).open('w') as f:p=subprocess.run(cmd,stdout=f,stderr=subprocess.STDOUT,cwd=root)
 r[name]={'command':cmd,'exit_code':p.returncode};(out/'regressions.json').write_text(json.dumps(r,indent=2)+'\n');print('RESULT',name,p.returncode,flush=True)
print('BASE SUITES COMPLETE',flush=True)
