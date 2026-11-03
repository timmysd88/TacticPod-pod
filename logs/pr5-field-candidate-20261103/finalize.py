from pathlib import Path
import json,hashlib,shutil,subprocess,zipfile,io
r=Path.cwd();o=r.parent/'logs/pr5-field-candidate-20261103';sha='3d4693207d459b1565af2e8b3955cd897f5d519c';h=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
results=json.loads((r/'.pio/reusable-regression-logs/results.json').read_text());assert len(results)==31 and all(v==0 for v in results.values()),results
shutil.copytree(r/'.pio/reusable-regression-logs',o/'regression-logs',dirs_exist_ok=True)
m=json.loads((o/'manifest.json').read_text());assert all(h(o/a['path'])==a['sha256'] for a in m['artifacts']);assert all(h(r/a['path'])==a['sha256'] for a in m['project_input_hashes']+m['framework_toolchain_references'])
refs=json.loads((o/'p6-preservation-before.json').read_text());assert all(h(r.parent/a['path'])==a['sha256'] for a in refs)
(o/'p6-preservation-result.json').write_text(json.dumps(dict(passed=True,files_checked=len(refs),all_recorded_files_unchanged=True),indent=2)+'\n')
# Preserve generated startup result outside the isolated source tree, then
# restore just that known output so the worktree matches the exact commit.
z=zipfile.ZipFile(io.BytesIO(subprocess.check_output(['git','-c','core.autocrlf=false','archive','--format=zip',sha])))
name='logs/stock-battery-review/compiled-startup-results.json'
if not (o/'compiled-startup-results.json').exists(): shutil.copy2(r/name,o/'compiled-startup-results.json')
(r/name).write_bytes(z.read(name))
name='tests/compact/fixtures/manifest.json';assert json.loads((r/name).read_text())==json.loads(z.read(name));generated=o/'generated-test-outputs'/name;generated.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(r/name,generated);(r/name).write_bytes(z.read(name))
d=[n for n in z.namelist() if not n.endswith('/') and (r/n).read_bytes()!=z.read(n)];assert not d,d
assert subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip()==sha
summary=dict(suite_count=31,passed=31,failed=0,results=results,focused_led_button='PASS',linked_startup='PASS',firmware_builds=2,hardware_tested=False,final_source_bytes_match_commit=True,p6_preserved=True)
(o/'regression-summary.json').write_text(json.dumps(summary,indent=2)+'\n');m.update(regressions=summary,software_validation_passed=True,source_bytes_match_commit=True,p6_preserved=True,p6_files_checked=len(refs),installation_authorized=False,uploaded=False,hardware_tested=False)
(o/'manifest.json').write_text(json.dumps(m,indent=2)+'\n')
p=o/'REPORT.md';s=p.read_text(encoding='utf-8').replace('Status: build/package/focused checks passed; final 31-suite collection pending.','Status: **software validation PASS**. Both builds, focused LED/button checks, linked startup and all 31 regression suites passed. Exact source and .p6 preservation audits passed.').replace('Full baseline 31-suite run: PENDING final collection.','Full baseline 31-suite run: **PASS, 31/31, exit code 0**, without firmware/test-source patches or linker adapters. See regression-summary.json and regression-logs/.');p.write_text(s,encoding='utf-8')
print('PASS: 31/31 suites; both builds; focused LED/button; linked startup; source identity; artifact hashes; .p6 preservation.')
for a in m['artifacts']:
 if a['environment']=='tacticpod_reusable_usb' and a['path'].endswith(('.bin','.zip')):print(a)
