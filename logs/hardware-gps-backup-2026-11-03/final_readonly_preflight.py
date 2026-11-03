"""Explicit final read-only preflight. No path to upload, wake, reset or recording."""
from pathlib import Path
from datetime import datetime, timezone
import hashlib
import importlib.util
import json

root = Path(__file__).resolve().parents[2]
out = Path(__file__).with_name('final-readonly-preflight-' + datetime.now(timezone.utc).strftime('%Y%m%d-%H%M%S'))
out.mkdir(exist_ok=False)
spec = importlib.util.spec_from_file_location('hw', Path(__file__).with_name('hardware_run.py'))
hw = importlib.util.module_from_spec(spec)
spec.loader.exec_module(hw)
hw.OUT = out
result = dict(outcome='BLOCKED', passed=False, owner_reported_recovery='Complete USB and battery power cycle',
    uploaded=False, gps_backup_testing_started=False, recording_started=False,
    erase_program_status_write_or_wake_sent=False, reset_sent=False, merged_or_pushed=False,
    upload_requires_further_explicit_approval=True)
monitor = None
try:
    result['candidate'] = hw.validate_candidate()
    print('Corrected candidate identity and checksums verified unchanged.', flush=True)
    monitor = hw.Monitor(out / 'serial-transcript.txt')
    # A read-only wait lets any existing startup/profile audit finish.
    result['initial'] = monitor.read(10)
    assert 'TPX3' not in result['initial'] and 'BEGIN_RECORD_CSV' not in result['initial'], 'Export was already active'
    result['before_states'] = monitor.idle()
    result['gps_status_existing_firmware'] = monitor.command('GPS STATUS', 'GPS speed_')
    result['battery'] = monitor.command('BAT STATUS', 'BAT source=')
    result['flash_identity_response'] = monitor.command('DIAG ID', 'END_FLASH_ID', 20)
    reference = (root / 'logs/phase3b4-hardware/sfdp-capture-1.bin').read_bytes()
    result['flash_validation'] = hw.validate_flash_identity(result['flash_identity_response'], reference)
    print('Flash PASS: five 0B4018 IDs, SR1 00, SR2 02, both SFDP passes match reference.', flush=True)
    result['recording_export'] = monitor.backup('recordings-readonly')
    image = (out / 'recordings-readonly-image.bin').read_bytes()
    baseline_dir = root.parent / 'logs/usb-control-test-2026-11-02'
    baseline = (baseline_dir / 'after-test-image.bin').read_bytes()
    baseline_decoded = json.loads((baseline_dir / 'after-test-decoded.json').read_text())
    current = json.loads((out / 'recordings-readonly-decoded.json').read_text())
    prefix_matches = len(image) >= len(baseline) and image[:len(baseline)] == baseline
    assert prefix_matches, 'Existing recordings differ from the saved baseline; inspect before any approval'
    old_issues = baseline_decoded['issues']
    assert current['issues'] == old_issues, 'New recording integrity issues; stop'
    old_ids = {s['session_id'] for s in baseline_decoded['sessions']}
    new_sessions = [s for s in current['sessions'] if s['session_id'] not in old_ids]
    assert all(s['status'] == 'complete' and not s['issues'] for s in new_sessions), 'New incomplete/corrupt session'
    result['integrity'] = dict(
        transport_packet_sequence_and_crc='PASS', decoder_page_block_footer_checks='PASS except unchanged known baseline issue',
        baseline_path=str(baseline_dir / 'after-test-image.bin'),
        baseline_bytes=len(baseline), baseline_sha256=hashlib.sha256(baseline).hexdigest(),
        baseline_recording_prefix_byte_identical=True,
        entire_exported_image_byte_identical=image == baseline,
        known_existing_issues=old_issues, new_issues=[],
        sessions=result['recording_export']['sessions'], new_sessions=[s['session_id'] for s in new_sessions])
    result['after_states'] = monitor.idle()
    result['passed'] = True
    result['outcome'] = 'READY FOR FLASH APPROVAL'
    print('Read-only recording integrity and final idle checks passed.', flush=True)
    print(json.dumps(result['integrity'], indent=2), flush=True)
except Exception as exc:
    result['error'] = str(exc)
    raise
finally:
    if monitor is not None:
        monitor.close()
    result['serial_released'] = True
    (out / 'result.json').write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    print('Outcome:', result['outcome'], '; evidence:', out, flush=True)
