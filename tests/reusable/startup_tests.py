"""Actual ARM variant execution with GPIO model; no electrical qualification."""
import sys, configparser
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tcb3'))
from native import Native,compile,ROOT
vendor=ROOT/'.pio-core/packages/framework-arduinoadafruitnrf52'
local=ROOT/'variants/tacticpod/Seeed_XIAO_nRF52840'
assert (local/'variant.h').read_bytes()==(vendor/'variants/Seeed_XIAO_nRF52840/variant.h').read_bytes()
core=(vendor/'cores/nRF5/main.cpp').read_text()
assert core.index('  init();')<core.index('  initVariant();')<core.index('  xTaskCreate(')
wiring=(vendor/'cores/nRF5/wiring.c').read_text()
assert 'NRF_P0->OUTSET = UINT32_MAX;' in wiring
digital=(vendor/'cores/nRF5/wiring_digital.c').read_text()
input_body=digital.split('case INPUT:')[1].split('break')[0]
assert 'GPIO_PIN_CNF_PULL_Disabled' in input_body
cfg=configparser.ConfigParser();cfg.read(ROOT/'platformio.ini')
assert cfg['env:tacticpod_reusable_usb']['board_build.variants_dir']=='variants/tacticpod'
for section in cfg.sections():
    if section!='env:tacticpod_reusable_usb':assert 'board_build.variants_dir' not in cfg[section]
elf=compile('tests/reusable/startup.cpp','battery-startup-tests',('tests/reusable/startup_hw',))
for i,name in enumerate(('USB-only startup','battery-present startup (model)',
    'USB charging startup (model)','inherited HIGH remains a pre-variant hazard',
    'old variant negative control','ten simulated resets')):
    code=Native(elf).call('startupCase',i);assert code==0,(name,code)
    print('PASS:',name,flush=True)
# Electrical inference only: ideal divider, not a measured clamp/injection model.
vb=4.2;vdd=3.3
low=vb*510/1510;high=(vb*510+vdd*1000)/1510
assert low<1.5 and high>vdd+.3
assert vb>vdd+.3  # Floating lower leg cannot be assumed safe.
print(f'PASS: ideal voltage hazard bounds LOW={low:.6f}V HIGH={high:.6f}V high-Z approaches {vb}V')
print('PASS: default build isolation, original pin map/header and core sequence checks')
