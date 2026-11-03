"""Source/evidence comparison only; no connection, wake, reset or upload."""
from pathlib import Path
import hashlib
import json
import re
import subprocess

ROOT = Path(__file__).resolve().parents[3]
OUT = Path(__file__).resolve().parent
WORKSPACE = ROOT.parent
BASE = '155a999122f0ce85ab6817c6eba690e9501fe4b3'

def committed(name):
    return subprocess.check_output(['git', 'show', BASE + ':' + name], cwd=ROOT).decode()

def body(text, signature):
    start = text.index(signature)
    brace = text.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]

current = (ROOT / 'src/main.cpp').read_text()
old = committed('src/main.cpp')
signatures = ['struct FlashIo', 'bool enterStandby()', 'void leaveStandby(uint32_t now)',
              'JedecId readFlashJedecId()', 'void readSfdp(uint32_t address',
              'bool identityRead(', 'bool identityIdle()', 'void updateIdentity()', 'void requestIdentity()']
comparison = {signature: body(current, signature) == body(old, signature) for signature in signatures}
assert all(comparison.values()), comparison
unchanged = ['src/flash_power.h', 'src/flash_identity.h', 'src/recorder.cpp', 'src/tcb3_session.cpp',
             'variants/tacticpod/Seeed_XIAO_nRF52840/variant.h',
             'variants/tacticpod/Seeed_XIAO_nRF52840/variant.cpp']
for name in unchanged:
    assert (ROOT / name).read_text() == committed(name), name
setup = body(current, 'void setup()')
order = ['digitalWrite(kFlashCsPin, HIGH)', 'pinMode(kFlashCsPin, OUTPUT)', 'SPI.begin()',
         'FlashPower::release(flash)', 'imu.begin()', 'Recorder::begin(imuReady)']
assert [setup.index(s) for s in order] == sorted(setup.index(s) for s in order)
diag_block = current[current.index('if(identityRequested){'):current.index('  updateIdentity();')]
assert 'FlashPower::release' not in diag_block
historical = json.loads((WORKSPACE / 'logs/field-retest-2026-11-02/post-upload-result.json').read_text())
previous = historical['flash_identity']
assert re.findall(r'FLASH ID JEDEC \d+ ([0-9A-F]+)', previous) == ['0B4018'] * 5
assert 'qualification=UNRESOLVED' in previous
serial = (OUT / 'readonly-serial.txt').read_text()
assert serial.count('manufacturer=0x00 memory_type=0x00 capacity_id=0x00') == 5
assert 'SFDP INVALID / unavailable' in serial
observed = json.loads((OUT / 'readonly-result.json').read_text())
observed.update(commands=re.findall(r'HOST TX ([^\n]+)', serial),
                legacy_diag=serial.split('HOST TX DIAG\n', 1)[1],
                collector_error='Host waited for healthy-SFDP summary marker, but firmware correctly took the invalid-SFDP early-return branch. Full reply is retained; this is not a proven SPI timeout.',
                raw_diag_in_this_run=False,
                raw_diag_reference='../hardware-run-20261103-retry1/preflight-result.json')
(OUT / 'readonly-result.json').write_text(json.dumps(observed, indent=2)+'\n', encoding='utf-8')
manifest = json.loads((ROOT / 'logs/hardware-gps-backup-2026-11-03/recovery-gate-fix/manifest.json').read_text())
assert all(hashlib.sha256((ROOT/f['path']).read_bytes()).hexdigest() == f['sha256']
           for f in manifest['changed_source_and_tests'] + manifest['artifacts'])
result = dict(working_source_commit=BASE, firmware_functions_unchanged=comparison,
    unchanged_files=unchanged, startup_order_verified=order,
    boot_release=True, standby_exit_release=True, release_opcode='AB', cs_high_after_release_us=100,
    diagnostic_releases_flash=False, flash_ready_readback_after_release=False,
    pins=dict(CS='D2 / P0.28', SCK='D8 / P1.13', MISO='D9 / P1.14', MOSI='D10 / P1.15'),
    protocol='SPI mode 0, MSB first, 1MHz; software CS; 9F then 3 result bytes; 5A then 24-bit address and one dummy byte',
    prior_good_diagnostic_also_printed_qualification_unresolved=True,
    host_preflight_qualification_rejection_is_incorrect=True,
    physical_root_cause='INCONCLUSIVE',
    leading_software_hypothesis='Flash left asleep or AB release not effective; not a demonstrated new source regression',
    no_new_firmware_changes=True, approved_candidate_hashes_unchanged=True,
    no_upload=True, no_gps_backup_test=True, no_flash_wake_reset_program_erase_or_status_write=True)
(OUT / 'source-comparison.json').write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
print(json.dumps(result, indent=2))
