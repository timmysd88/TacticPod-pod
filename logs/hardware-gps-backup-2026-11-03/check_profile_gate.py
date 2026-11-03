"""Read-only source proof of PR #6's circular profile gate; no device access."""
from pathlib import Path
import json
import re

root = Path(__file__).resolve().parents[2]
gps = (root / 'src/gps_receiver.cpp').read_text()
power = (root / 'src/gps_power.h').read_text()
step = re.search(r'static void backupStep\(\)\s*\{.*?\n\}', gps, re.S).group()
ready = re.search(r'bool recordingReady\(\)\s*\{.*?\n\}', gps, re.S).group()
assert 'o.profileVerified = recordingReady();' in step
assert '&& !backupLatched && !backupTestRunning()' in ready
assert 'bool running() const { return phase != Phase::Idle && phase != Phase::Done; }' in power
profile = power.split('case Phase::Profile:')[1].split('case Phase::Fix:')[0]
assert 'if (o.profileVerified)' in profile
assert 'el >= kProfileMs' in profile and 'finish(Result::ProfileFailed)' in profile
result = {
    'source_commit': '29f90966d868f68b85d63d64d27bab9584f7628a',
    'source_verification': 'FAIL',
    'physical_verification': 'NOT_RUN',
    'finding': 'Profile phase is running, so recordingReady() is false and o.profileVerified cannot become true. The profile phase must fail or time out even with a recovered receiver.',
    'timeout_ms': 45000,
    'device_accessed': False,
    'firmware_modified': False,
}
Path(__file__).with_name('profile-gate-finding.json').write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps(result, indent=2))
