"""Compare tracked content with Git blobs; report checkout changes separately."""
from pathlib import Path
import io
import json
import subprocess
import zipfile

root = Path(__file__).resolve().parents[3]
archive = zipfile.ZipFile(io.BytesIO(subprocess.check_output(
    ['git', '-c', 'core.autocrlf=false', 'archive', '--format=zip', 'HEAD'], cwd=root)))
differences = []
for name in archive.namelist():
    p = root / name
    if name.endswith('/') or not p.is_file():
        continue
    if p.read_bytes().replace(b'\r\n', b'\n') != archive.read(name).replace(b'\r\n', b'\n'):
        differences.append(name)
out = Path(__file__).with_name('tracked-content-differences.json')
out.write_text(json.dumps(differences, indent=2)+'\n')
print(json.dumps(differences, indent=2))
