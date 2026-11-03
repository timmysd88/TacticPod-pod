"""Curate file snapshots only. Never touches hardware, branches, or source inputs."""
from pathlib import Path
import subprocess,hashlib,json,re,collections
ROOT=Path(__file__).resolve().parents[1]
FIELD=ROOT/'.p5';EXP=ROOT/'.s6'
TEXT={'.md','.txt','.json','.py','.ps1','.log','.ini','.patch','.csv','.cpp','.h','.yml','.yaml'}
SKIP={'toolchain','node_modules','npm-cache','__pycache__','.pio','.pio-core','main-snapshot'}
CANONICAL={'logs/pr5-field-test-60s-20261103/before-test.tpx3','logs/pr5-field-test-60s-20261103/after-test.tpx3','logs/pr5-field-candidate-20261103/tacticpod_reusable_usb/firmware.bin','logs/pr5-field-candidate-20261103/tacticpod_reusable_usb/firmware.zip'}
secret=re.compile(rb'-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----|gh[pousr]_[A-Za-z0-9]{30,}|github_pat_[A-Za-z0-9_]{40,}|AKIA[0-9A-Z]{16}')
def git(path,*args):return subprocess.check_output(['git','-c','core.autocrlf=false','-c','core.longpaths=true','-C',str(path),*args],stderr=subprocess.DEVNULL)
def digest(data):return hashlib.sha256(data).hexdigest()
def untracked(p):return [x for x in git(p,'ls-files','--others','--exclude-standard','-z').decode().split('\0') if x]
def row(p,rel):
 data=p.read_bytes();return dict(path=rel,bytes=len(data),sha256=digest(data))
def reason(rel,p):
 if any(x in SKIP for x in Path(rel).parts):return 'toolchain/cache or reproducible source snapshot'
 if p.name.startswith('.env') or re.search(r'(?i)(credential|id_rsa|id_ed25519|\.pem$|\.key$)',p.name):return 'potential credential: excluded without publishing contents'
 if rel in CANONICAL:return None
 if p.suffix.lower() not in TEXT:return 'generated binary/duplicate recording/export; canonical PR5 package and two latest TPX3 exports retained'
 if p.stat().st_size>250000:return 'large derived text/decoded data; original TPX3 or summary retained'
 if b'\0' in p.read_bytes()[:4096]:return 'non-text content'
 return None
def write_snapshot(src,dst):
 data=src.read_bytes()
 if src.suffix.lower() in TEXT and secret.search(data):raise RuntimeError('Secret signature in '+str(src))
 dst.parent.mkdir(parents=True,exist_ok=True)
 assert not dst.exists(),'Refusing overwrite '+str(dst)
 dst.write_bytes(data)
 included=[]
 return data
def save(p,obj):p.parent.mkdir(parents=True,exist_ok=True);p.write_text(json.dumps(obj,indent=2)+'\n',encoding='utf-8')
field_rows=[];excluded=[]
for rel in untracked(ROOT):
 if rel.startswith(('.p5/','.p6/','.s6/','.worktrees/')):continue
 p=ROOT/rel
 if not p.is_file():continue
 why=reason(rel,p)
 if why:excluded.append(dict(scope='root',reason=why,**row(p,rel)));continue
 data=write_snapshot(p,FIELD/rel);field_rows.append(dict(source_scope='root',**row(p,rel)))
exp_rows=[]
changed=[]
for line in git(ROOT/'.p6','diff','--ignore-space-at-eol','--numstat').decode().splitlines():
 a,d,rel=line.split('\t',2)
 if int(a)+int(d)>0:changed.append(rel)
for rel in changed+untracked(ROOT/'.p6'):
 p=ROOT/'.p6'/rel
 if not p.is_file():continue
 why=None if rel in changed else reason(rel,p)
 if why:excluded.append(dict(scope='.p6',reason=why,**row(p,rel)));continue
 data=p.read_bytes()
 if secret.search(data):raise RuntimeError('Secret signature in '+str(p))
 dest=EXP/rel;dest.parent.mkdir(parents=True,exist_ok=True)
 if dest.exists():assert rel in changed,'Unexpected destination collision '+rel
 dest.write_bytes(data)
 exp_rows.append(dict(source_scope='.p6',**row(p,rel)))
for rel in untracked(ROOT/'.worktrees/pr6-gpsbackup-20261103'):
 p=ROOT/'.worktrees/pr6-gpsbackup-20261103'/rel
 if p.is_file():
  dest='logs/shutdown-20261103/initial-long-path/'+p.name
  write_snapshot(p,EXP/dest);exp_rows.append(dict(source_scope='.worktrees/pr6-gpsbackup-20261103',source_path=rel,**row(p,dest)))
for scope in ('.','.p6','.worktrees/pr6-gpsbackup-20261103'):
 p=ROOT/scope
 # Record all remaining tracked differences without staging or resetting them.
 status=git(p,'status','--porcelain','--untracked-files=no').decode()
 actual=git(p,'diff','--ignore-space-at-eol','--numstat').decode()
 save(FIELD/('logs/shutdown-20261103/'+{'.':'root','.p6':'experimental','.worktrees/pr6-gpsbackup-20261103':'long-path'}[scope]+'-initial-status.json'),dict(scope=scope,status=status,substantive_diff=actual))
summary=dict(field_files=len(field_rows),field_bytes=sum(x['bytes'] for x in field_rows),experimental_files=len(exp_rows),experimental_bytes=sum(x['bytes'] for x in exp_rows),excluded_files=len(excluded),excluded_bytes=sum(x['bytes'] for x in excluded),substantive_experimental_files=changed)
save(FIELD/'logs/shutdown-20261103/preserved-files.json',field_rows)
save(FIELD/'logs/shutdown-20261103/local-only-files.json',excluded)
save(FIELD/'logs/shutdown-20261103/summary.json',summary)
save(EXP/'logs/shutdown-20261103/preserved-files.json',exp_rows)
save(EXP/'logs/shutdown-20261103/local-only-files.json',[x for x in excluded if x['scope']=='.p6'])
print(json.dumps(summary,indent=2))
