"""Stage explicitly curated snapshots; metadata writes only to backup branches."""
from pathlib import Path
import subprocess,json
R=Path(__file__).resolve().parents[1]
for scope,branch in (('.p5','backup/pr5-field-handover-20261103'),('.s6','backup/gps-experiment-paused-20261103')):
 p=R/scope
 def git(*a,**kw):return subprocess.run(['git','-c','core.autocrlf=false','-c','core.longpaths=true','-C',str(p),*a],check=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE,**kw).stdout
 assert git('branch','--show-current').decode().strip()==branch
 assert not git('diff','--cached','--name-only')
 paths=[x for x in git('ls-files','--others','--exclude-standard','-z').decode().split('\0') if x]
 if scope=='.s6':
  paths+=json.loads((R/'.p5/logs/shutdown-20261103/summary.json').read_text())['substantive_experimental_files']
 else:assert all(x.startswith(('logs/','docs/')) for x in paths)
 git('add','--pathspec-from-file=-','--pathspec-file-nul',input=b'\0'.join(x.encode() for x in paths)+b'\0')
 staged=git('diff','--cached','--name-only').decode().splitlines()
 if scope=='.p5':assert all(x.startswith(('logs/','docs/')) for x in staged)
 print(scope,len(staged),'staged paths; bytes',sum((p/x).stat().st_size for x in staged))
 print(git('diff','--cached','--shortstat').decode().strip())
