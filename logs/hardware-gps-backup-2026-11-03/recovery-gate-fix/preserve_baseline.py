from pathlib import Path
import json
import shutil

root = Path(__file__).resolve().parents[3]
out = Path(__file__).resolve().parent
baseline = out.parent
shutil.copy2(root / 'docs/hardware-gps-backup-test-report.md', out / 'baseline-report.md')
manifest_path = baseline / 'preflight-manifest.json'
manifest = json.loads(manifest_path.read_text())
for artifact in manifest['artifacts']:
    original = Path(artifact['path'])
    if original.parts[0] != '.pio':
        continue
    target = baseline / 'baseline-artifacts' / original.parent.name / original.name
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(root / original, target)
    artifact['path'] = str(target.relative_to(root))
manifest_path.write_text(json.dumps(manifest, indent=2) + '\n')
(baseline / 'SHA256SUMS.txt').write_text(''.join(a['sha256'] + '  ' + a['path'] + '\n' for a in manifest['artifacts']))
print('Original report and build artifacts preserved')
