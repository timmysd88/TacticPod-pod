"""Seal the successful read-only preflight and update reports; no device actions."""
from pathlib import Path
import hashlib
import json
import re

root = Path(__file__).resolve().parents[2]
out = Path(__file__).with_name('final-readonly-preflight-20261103-091526')
r = json.loads((out / 'result.json').read_text())
assert r['passed'] and r['outcome'] == 'READY FOR FLASH APPROVAL'
decoded = json.loads((out / 'recordings-readonly-decoded.json').read_text())
assert not decoded['legacy']['issues']
assert r['before_states']['RECORD STATUS'] == r['after_states']['RECORD STATUS']
assert r['before_states']['TCB STATUS'] == r['after_states']['TCB STATUS']
assert 'programs=0 ' in r['before_states']['TCB STATUS']
pointer = int(re.search(r'next_sector=0x([0-9A-F]+)', r['before_states']['RECORD STATUS'])[1], 16)
s159 = next(s for s in decoded['sessions'] if s['session_id'] == 159)
assert s159['status'] == 'complete' and not s159['issues']
assert max(s159['sectors']) + 4096 <= pointer
r['integrity'].update(legacy_decoder_issues=[], idle_and_append_pointer_unchanged=True,
    append_pointer_hex=f'{pointer:06X}', session159_already_within_preflight_recovered_append_range=True,
    source_of_session159='Pre-existing data; not created during this read-only run; origin not inferred')
manifest = json.loads((Path(__file__).parent / 'recovery-gate-fix/manifest.json').read_text())
assert all(hashlib.sha256((root / a['path']).read_bytes()).hexdigest() == a['sha256']
           for a in manifest['changed_source_and_tests'] + manifest['artifacts'])
r['candidate_unchanged_after_preflight'] = True
(out / 'result.json').write_text(json.dumps(r, indent=2)+'\n', encoding='utf-8')

report = root / 'docs/hardware-gps-backup-test-report.md'
text = report.read_text(encoding='utf-8')
text = text.replace('**Latest read-only flash identification PASS: communication restored. Firmware uploads and GPS backup testing remain blocked by the owner\'s instruction.**',
    '**READY FOR FLASH APPROVAL — final read-only preflight passed. No upload or GPS backup test is authorized without further explicit approval.**')
text += '''

## Final read-only preflight after complete power cycle — READY FOR FLASH APPROVAL

The owner reports recovering flash communication by removing **both USB and battery power**, then reconnecting. This history supports investigating retained device/power state, but does not prove deep power-down was the specific cause. No reset or power action was performed by the assistant.

The owner requested a final read-only preflight and expressly required stopping for **further explicit flash approval** afterward. The following checks passed against the existing installed firmware, not a newly uploaded test image:

| Gate | Final result |
| --- | --- |
| Device | COM8, 115200 baud, VID:PID `2886:8044`, serial `0E8A89B7289430BD`. |
| Idle before and after download | V1 STOPPED, OBS/fast IMU OFF, COMP idle/no fault/empty queues, TCB state=1/session=0/error=none/lock=0/programs=0. Recording append pointer remained `0x0A1000`. The complete export ended and status commands responded normally afterward; no export remains active. |
| Flash communication | Five identical `0B4018` reads. SR1 before/after `00`, SR2 `02`. Both complete 256-byte SFDP passes matched the saved reference exactly; header `53 46 44 50 00 01 01 FF`. Informational qualification text did not determine admission. |
| Read-only recording export | Complete TPX3 download, 721,344 bytes, 2,578 packets; packet sequence/end framing and CRC checks passed. Export SHA-256 `9cdc66e807012d2330439fde8761e3137cc7a3431627269c856fd2974c39bb72`. Reconstructed image 659,456 bytes, SHA-256 `d1c398b03deee2e8ea88a86779e856154ab0dfbac4a1165df1f62faf678e0379`. |
| Older recordings preserved | Entire 647,168-byte recording prefix from the saved USB-control baseline is byte-identical, SHA-256 `e8e62ef464f9f5e4a1f116a7d8eaa7806c3bc7152443e1cb5bafc2f10c0f2848`. All 13 baseline sessions remain preserved. |
| Available integrity checks | Transport/page/block/footer decoding found **no new issues**; legacy decoder issues empty. The known historical interrupted final block in session 133 at `0x087000` is unchanged. This is not a claim that every historical session is complete. |
| Additional pre-existing data | Session 159 is complete, with 1,000 IMU / 100 RMC / 10 GGA records, offered=committed, lost/rejected/missed/CRC/incomplete/full counters zero. It is additional to the older baseline, not created in this run: it lies within the append range already recovered before the preflight, and before/after TCB programs=0/session=0 and append pointer are unchanged. Its origin is not inferred. Therefore the *entire* current image is not byte-identical to the smaller older image; its entire older prefix is. |
| Existing-firmware GPS context | Valid checksum NMEA at 38400, CURRENT RAM readback verified 100 ms / RMC 10 Hz / GGA 1 Hz; valid fix at observation. No GPS backup command or new cadence test was run. |
| Corrected candidate | All candidate source/test hashes, unchanged remaining firmware/variant/config bytes, local patch identity and selected build artifacts still match the tested manifest. The candidate was not rebuilt or uploaded. Automatic GPS backup remains off in the selected environment. |

Candidate environment: `tacticpod_reusable_usb_gpsbackup`.

- Base commit: `29f90966d868f68b85d63d64d27bab9584f7628a`, plus the same uncommitted recovery-gate correction.
- Patch SHA-256: `0a4ffaa21e61a320e9e30ef2f3b79271afc2f757808063fd0c1312c876aa8eaa`.
- Application BIN SHA-256: **`aced1874981173ebfbb3904390f6088214aca3419f72f6a5fae0deb4f4c14051`**.
- Application-only DFU ZIP SHA-256: **`91727831db63198d2c007134803526125a096adc47eeecbe1d8c3879e2004e28`**.
- ELF-to-BIN conversion matches the ZIP payload; all manifest artifact/source hashes were checked again after the preflight.

Evidence directory: `logs/hardware-gps-backup-2026-11-03/final-readonly-preflight-20261103-091526/`. It contains the full serial transcript, candidate verification, complete TPX3 archive, reconstructed image, decoded records and `result.json`. The status transcript records all commands; none start/stop a recording, wake/reset, erase, program, alter status/configuration, upload, merge or push. Serial was closed.

**Result: READY FOR FLASH APPROVAL.** This means the final read-only preflight passed, with the explicit historical session-133 caveat. It is not approval to flash and does not qualify GPS backup operation or pilot/production readiness. The assistant stopped here. Firmware upload, GPS backup cycles, merge and push require further explicit owner approval; previous upload permission is not reused for this next step under the owner's latest instruction.
'''
report.write_text(text, encoding='utf-8')
investigation = root / 'docs/flash-detection-investigation.md'
text = investigation.read_text(encoding='utf-8')
text += '''

## Final owner-directed read-only preflight after full power cycle

The owner subsequently confirmed a complete USB-and-battery power cycle. A final read-only preflight passed: five `0B4018` IDs, SR1 `00`, SR2 `02`, two full SFDP passes matching the reference, idle recorders before/after, a CRC-valid recording export, and the complete older 647,168-byte recording prefix preserved byte-for-byte. The historical interrupted session 133 remains unchanged; complete session 159 was already present before this preflight, with no new integrity issues. The corrected candidate source/patch/BIN/ZIP hashes remain unchanged. Evidence and exact checksums are in `hardware-gps-backup-test-report.md` and `logs/hardware-gps-backup-2026-11-03/final-readonly-preflight-20261103-091526/`.

**READY FOR FLASH APPROVAL**, then stopped as requested. No upload or GPS backup test occurred. Further explicit approval is required before proceeding. Power-cycle recovery is now owner-reported fact; the particular electrical or retained-state cause remains unproven.
'''
investigation.write_text(text, encoding='utf-8')
print('READY FOR FLASH APPROVAL — report and evidence finalized; no further device action')
