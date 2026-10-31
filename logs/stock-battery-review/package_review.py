"""Freeze the stock-board USB-only review without modifying firmware or hardware."""
from pathlib import Path
import sys,json,hashlib,shutil,zipfile,subprocess
ROOT=Path(__file__).resolve().parents[2];OUT=Path(__file__).resolve().parent
sys.path.insert(0,str(ROOT/'.pio-core/test-python'))
from elftools.elf.elffile import ELFFile
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
old=json.loads((ROOT/'logs/battery-startup-review/source-hashes.json').read_text())
protected={p:h for p,h in old.items() if p.startswith(('src/','variants/')) or p=='platformio.ini'}
assert all(sha(ROOT/p)==h for p,h in protected.items())
results=json.loads((OUT/'regressions.json').read_text())
assert len(results)==7 and all(c==0 for c in results.values()),results
before=json.loads((ROOT/'logs/battery-startup-review/artifacts.json').read_text())
artifacts={}
build=ROOT/'.pio/build/tacticpod_reusable_usb'
for suffix in ('elf','hex','zip'):
    src=build/('firmware.'+suffix);assert sha(src)==before[suffix]
    dst=OUT/('tacticpod_reusable_usb-stock-board.'+suffix)
    shutil.copyfile(src,dst);artifacts[suffix]=sha(dst)
with zipfile.ZipFile(build/'firmware.zip') as z:
    manifest=json.loads(z.read('manifest.json'))['manifest']
    assert set(manifest)=={'application','dfu_version'}
    artifacts['manifest']=manifest
default=ROOT/'.pio/build/seeed_xiao_nrf52840_sense/firmware.elf'
assert sha(default)==before['default_elf']
artifacts['default_elf']=sha(default)
objdump=ROOT/'.pio-core/packages/toolchain-gccarmnoneeabi/bin/arm-none-eabi-objdump.exe'
assembly=subprocess.check_output([str(objdump),'-d','-C',str(build/'firmware.elf')],text=True)
for symbol in ('initVariant()','init','main'):
    body=assembly.split('<'+symbol+'>:\n')[1].split('\n\n')[0]
    (OUT/(symbol.replace('()','')+'-disassembly.txt')).write_text(body+'\n')
with (build/'firmware.elf').open('rb') as f:
    e=ELFFile(f);table=list(e.get_section_by_name('.symtab').iter_symbols())
    symbols={s['st_value']&~1:s.name for s in table if s['st_info']['type']=='STT_FUNC'}
    byname={s.name:s['st_value'] for s in table}
    start,end=byname['__init_array_start'],byname['__init_array_end']
    raw=b''
    for segment in e.iter_segments():
        base=segment['p_vaddr'];data=segment.data()
        if base<=start<=end<=base+len(data):raw=data[start-base:end-base];break
    assert len(raw)==end-start
    constructors=[{'address':hex(int.from_bytes(raw[i:i+4],'little')),
        'symbol':symbols.get(int.from_bytes(raw[i:i+4],'little')&~1,'unknown')} for i in range(0,len(raw),4)]
(OUT/'constructors.json').write_text(json.dumps(constructors,indent=2)+'\n')
baseline=json.loads((ROOT/'logs/battery-startup-review/preserved-physical-data.json').read_text())
assert all(sha(ROOT/p)==h for p,h in baseline.items())
summary={'targeted_suites_passed':len(results),'previous_complete_suites_passed':26,
    'previous_complete_evidence':'logs/battery-startup-review/regressions.json',
    'production_sources_unchanged':True,'default_elf_unchanged':True,
    'ram_bytes':120836,'program_bytes':189424,
    'hardware_modified':False,'device_accessed':False,'uploaded':False,
    'bootloader_modified':False,'battery_connection_qualified':False,
    'decision':'GO for separately authorised battery-absent USB-only test; battery connection remains unqualified',
    'artifacts':artifacts,'protected_source_hashes':protected,'preserved_archive_hashes':baseline}
(OUT/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
print(json.dumps({'artifacts':artifacts,'suites_passed':len(results)},indent=2))
