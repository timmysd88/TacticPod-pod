"""Prove the new integration test rejects the original circular observation.

Mutation exists only in a generated preprocessed test translation unit.
No firmware/test source is overwritten.
"""
from pathlib import Path
import json
import subprocess
import sys

root = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(root / 'tests/tcb3'))
from native import compile, Native

compiler = root / '.pio-core/packages/toolchain-gccarmnoneeabi/bin/arm-none-eabi-g++.exe'
include = ['-I' + str(root / p) for p in ('tests/recording', 'tests/flash_write', 'lib/TinyGPSPlus/src')]
preprocessed = subprocess.check_output([str(compiler), '-E', '-std=c++11', '-DARDUINO=10819',
    '-mcpu=cortex-m4', '-mthumb', *include, str(root / 'tests/reusable/gps_backup_integration.cpp')])
old = b'o.profileVerified = backupRecoveryVerified();'
assert preprocessed.count(old) == 1
mutated = preprocessed.replace(old, b'o.profileVerified = recordingReady();')
(root / '.pio/backup-old-gate.ii').write_bytes(mutated)
elf = compile('.pio/backup-old-gate.ii', 'backup-old-gate',
              extra=(root / 'lib/TinyGPSPlus/src/TinyGPS++.cpp', *include))
machine = Native(elf)
code = machine.call('backupIntegrationCase', 0, limit=4000000000)
assert code == 11, ('original gate should fail successful-recovery integration case', code)
result = dict(result='PASS', original_gate_rejected=True, integration_case=0,
              observed_failure_code=code, production_sources_modified=False)
Path(__file__).with_suffix('.json').write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps(result, indent=2))
