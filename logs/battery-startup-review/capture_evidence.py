"""Offline build/source evidence only. Never opens a device or upload tool."""
from pathlib import Path
import hashlib,json,subprocess,shutil,zipfile,difflib,sys
ROOT=Path(__file__).resolve().parents[2]
OUT=Path(__file__).resolve().parent
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def save(name,value):(OUT/name).write_text(json.dumps(value,indent=2)+'\n')
old=json.loads((ROOT/'logs/reusable-review/final-source-hashes.json').read_text())
changed={p:{'before':h,'after':sha(ROOT/p)} for p,h in old.items() if sha(ROOT/p)!=h}
assert set(changed)=={'platformio.ini','docs/reusable-prototype.md','docs/battery-bench-checklist.md','tests/reusable/run_all_tests.py'},changed
oldbsp=json.loads((ROOT/'logs/reusable-review/hardware-bsp-hashes.json').read_text())
assert all(sha(ROOT/p)==h for p,h in oldbsp.items())
default=ROOT/'.pio/build/seeed_xiao_nrf52840_sense/firmware.elf'
assert sha(default)=='a83cbaabf9c0e18034a5f70a987539c77c574e429e9b6aa3a06caf6d1a836089'
vendor=ROOT/'.pio-core/packages/framework-arduinoadafruitnrf52'
sources=list((ROOT/'src').glob('*'))+list((ROOT/'variants/tacticpod').rglob('*'))
sources += [ROOT/'platformio.ini', ROOT/'docs/battery-startup-safety.md', ROOT/'docs/battery-sense-hardware-options.md',ROOT/'tests/reusable/startup.cpp',ROOT/'tests/reusable/startup_tests.py', ROOT/'tests/reusable/run_all_tests.py']
sources += list((ROOT/'tests/reusable/startup_hw').glob('*'))
save('source-hashes.json',{str(p.relative_to(ROOT)).replace('\\','/'):sha(p) for p in sources if p.is_file()})
bsp=dict(oldbsp)
for f in ('main.cpp','wiring.c','wiring_digital.c','linker/gcc_startup_nrf52840.S','nordic/nrfx/mdk/system_nrf52840.c'):
    p=vendor/'cores/nRF5'/f;bsp[str(p.relative_to(ROOT)).replace('\\','/')]=sha(p)
save('bsp-hashes.json',bsp);save('changes-from-reusable-baseline.json',changed)
artifacts={'default_elf':sha(default)}
build=ROOT/'.pio/build/tacticpod_reusable_usb'
for suffix in ('elf','hex','zip'):
    src=build/('firmware.'+suffix);dst=OUT/('tacticpod_reusable_usb-battery-startup.'+suffix)
    shutil.copyfile(src,dst);artifacts[suffix]=sha(dst)
with zipfile.ZipFile(build/'firmware.zip') as z:
    artifacts['zip_members']=z.namelist()
    manifest=json.loads(z.read('manifest.json'))['manifest']
    assert set(manifest)=={'application','dfu_version'},manifest
    app=manifest['application']
    artifacts['application_sha256']=hashlib.sha256(z.read(app['bin_file'])).hexdigest()
    assert set(z.namelist())=={app['bin_file'],app['dat_file'],'manifest.json'}
save('artifacts.json',artifacts)
objdump=ROOT/'.pio-core/packages/toolchain-gccarmnoneeabi/bin/arm-none-eabi-objdump.exe'
asm=subprocess.check_output([str(objdump),'-d','-C',str(build/'firmware.elf')],text=True)
body=asm.split('<initVariant()>:\n')[1].split('\n\n')[0]
assert body.index('<digitalWrite>')<body.index('<pinMode>')
(OUT/'initVariant-disassembly.txt').write_text(body+'\n')
v=vendor/'variants/Seeed_XIAO_nRF52840/variant.cpp'
l=ROOT/'variants/tacticpod/Seeed_XIAO_nRF52840/variant.cpp'
(OUT/'variant.diff').write_text(''.join(difflib.unified_diff(v.read_text().splitlines(True),l.read_text().splitlines(True),fromfile='vendor/variant.cpp',tofile='project/variant.cpp')))
save('preserved-physical-data.json',{str(p.relative_to(ROOT)).replace('\\','/'):sha(p) for p in (
    ROOT/'logs/phase3b10-hardware/recovered-recording-image.bin',
    ROOT/'logs/phase3b10-hardware/movement-120s-1.tpx3',
    ROOT/'logs/phase3b10-hardware/movement-120s-2.tpx3',
    ROOT/'logs/phase3b10-review/tcb3_movement_120s-3b10.zip',
    ROOT/'logs/reusable-review/tacticpod_reusable_usb.zip')})
print(json.dumps(artifacts,indent=2))
if '--finalize' in sys.argv:
    results=json.loads((ROOT/'.pio/reusable-regression-logs/results.json').read_text())
    assert len(results)==26 and all(code==0 for code in results.values()),results
    save('regressions.json',results)
    shutil.copytree(ROOT/'.pio/reusable-regression-logs',OUT/'regression-logs',dirs_exist_ok=True)
    save('validation.json',{'suites_passed':len(results),'suites_failed':0,
        'builds':['seeed_xiao_nrf52840_sense','tacticpod_reusable_usb'],
        'static_ram':120836,'program_bytes':189424,
        'hardware_access':False,'battery_connected':False,'upload_performed':False,
        'battery_connection_decision':'NO-GO',
        'preferred_hardware_solution':'Isolate original VBAT-to-ADC path; qualified external single-cell I2C voltage monitor'})
