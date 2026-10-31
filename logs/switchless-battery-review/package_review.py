"""Offline evidence freeze; this script never opens serial or invokes an uploader."""
from pathlib import Path
import hashlib,json,shutil,zipfile
ROOT=Path(__file__).resolve().parents[2];OUT=Path(__file__).resolve().parent
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
stock=json.loads((ROOT/'logs/stock-battery-review/summary.json').read_text())
assert all(sha(ROOT/p)==h for p,h in stock['protected_source_hashes'].items())
assert all(sha(ROOT/p)==h for p,h in stock['preserved_archive_hashes'].items())
tests=json.loads((OUT/'regressions.json').read_text())
assert len(tests)==9 and all(c==0 for c in tests.values())
assert (OUT/'gps-exit.txt').read_text().strip()=='0'
tests['tests/gps/run_tests.py']=0
(OUT/'all-results.json').write_text(json.dumps(tests,indent=2)+'\n')
artifacts={}
for suffix in ('elf','hex','zip'):
    p=ROOT/'.pio/build/tacticpod_reusable_usb'/('firmware.'+suffix)
    assert sha(p)==stock['artifacts'][suffix]
    dst=OUT/('tacticpod_reusable_usb-switchless.'+suffix);shutil.copyfile(p,dst)
    artifacts[suffix]=sha(dst)
with zipfile.ZipFile(OUT/'tacticpod_reusable_usb-switchless.zip') as z:
    assert set(json.loads(z.read('manifest.json'))['manifest'])=={'application','dfu_version'}
assert sha(ROOT/'.pio/build/seeed_xiao_nrf52840_sense/firmware.elf')==stock['artifacts']['default_elf']
for src,dst in (('.pio/switchless-example.tpx3','simulated-switchless.tpx3'),('.pio/switchless-results.json','simulated-results.json')):
    shutil.copyfile(ROOT/src,OUT/dst)
changed=[ROOT/'tools/tcb3_download.py',ROOT/'tools/verify_tcb3_append.py',
    ROOT/'tests/reusable/switchless.cpp',ROOT/'tests/reusable/switchless_tests.py',
    ROOT/'tests/reusable/download_tests.py',ROOT/'tests/reusable/run_all_tests.py',
    ROOT/'docs/switchless-battery-first-test.md',ROOT/'docs/reusable-prototype.md']
source_hashes=dict(stock['protected_source_hashes'])
source_hashes.update({str(p.relative_to(ROOT)).replace('\\','/'):sha(p) for p in changed})
summary={'suites_passed_current':len(tests),'suites_failed':0,'previous_complete_suites_passed':26,
    'production_code_changed':False,'default_build_changed':False,
    'ram_bytes':120836,'program_bytes':189424,'artifacts':artifacts,
    'recording_partition_write_window':'0x039000-0xFFEFFF (preserved-prefix reusable profile)',
    'diagnostic_sector':'0xFFF000-0xFFFFFF excluded',
    'physical_device_access':False,'firmware_uploaded':False,'battery_connected':False,
    'software_decision':'GO for review and separately authorised USB-only bench verification',
    'electrical_battery_qualification':'Unresolved; no claim of safe LiPo operation',
    'source_hashes':source_hashes,'preserved_archive_hashes':stock['preserved_archive_hashes']}
(OUT/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
print(json.dumps({'suites_passed':len(tests),'artifacts':artifacts},indent=2))
