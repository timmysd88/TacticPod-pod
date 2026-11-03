"""Offline admission regressions; no device connection and no uploader execution."""
from pathlib import Path
import importlib.util
import json
import re

root = Path(__file__).resolve().parents[2]
out = Path(__file__).with_name('flash-wake-capability-check')
spec = importlib.util.spec_from_file_location('hw', Path(__file__).with_name('hardware_run.py'))
hw = importlib.util.module_from_spec(spec)
spec.loader.exec_module(hw)
reference = (root / 'logs/phase3b4-hardware/sfdp-capture-1.bin').read_bytes()
previous = json.loads((root.parent / 'logs/field-retest-2026-11-02/post-upload-result.json').read_text())['flash_identity']
assert 'qualification=UNRESOLVED' in previous
results = {}

def accepts(name, text):
    result = hw.validate_flash_identity(text, reference)
    assert result['passed'] and not result['qualification_label_used_for_admission']
    results[name] = 'PASS'

def rejects(name, text, saved=reference):
    try:
        hw.validate_flash_identity(text, saved)
    except ValueError:
        results[name] = 'PASS'
    else:
        raise AssertionError('Invalid evidence admitted: ' + name)

accepts('known-good hardware capture with qualification UNRESOLVED', previous)
accepts('qualification text is informational', previous.replace('qualification=UNRESOLVED', 'qualification=OTHER'))
failed = json.loads((Path(__file__).parent / 'hardware-run-20261103-retry1/preflight-result.json').read_text())['identity']
rejects('actual all-zero failed-device capture with repeatability PASS', failed)
rejects('one inconsistent JEDEC result', previous.replace('FLASH ID JEDEC 03 0B4018', 'FLASH ID JEDEC 03 0B4019'))
rejects('all-FF JEDEC results', previous.replace('0B4018', 'FFFFFF'))
rejects('busy status despite valid JEDEC/SFDP', previous.replace('SR1 before=00', 'SR1 before=01'))
rejects('changed protection/status state', previous.replace('SR2=02', 'SR2=00'))
rejects('missing SFDP chunk', re.sub(r'FLASH ID SFDP pass=01 address=0010 [0-9A-F]+\n', '', previous))
rejects('corrupt SFDP signature', previous.replace('53464450', '00464450'))
rejects('incomplete capture', previous.replace('END_FLASH_ID', 'MISSING_END'))
rejects('invalid saved reference', previous, bytes(256))
(out / 'host-preflight-test-results.json').write_text(json.dumps(dict(
    passed=len(results), failed=0, tests=results, hardware_accessed=False,
    uploader_executed=False), indent=2)+'\n', encoding='utf-8')
print(json.dumps(results, indent=2))
