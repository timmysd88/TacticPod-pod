"""Execute linked core init/variant functions, not replacements; GPIO model only."""
from pathlib import Path
import sys,json,hashlib,struct
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'.pio-core/test-python'))
from elftools.elf.elffile import ELFFile
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_THUMB,UC_MODE_MCLASS,UC_HOOK_MEM_WRITE
from unicorn.arm_const import UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_PC
out=ROOT/'logs/stock-battery-review';out.mkdir(exist_ok=True)

def run(profile,inherited=False):
    path=ROOT/'.pio/build'/profile/'firmware.elf'
    with path.open('rb') as f:
        elf=ELFFile(f)
        symbols={s.name:s['st_value'] for s in elf.get_section_by_name('.symtab').iter_symbols()}
        segments=[(s['p_vaddr'],s.data()) for s in elf.iter_segments() if s['p_type']=='PT_LOAD']
    m=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS)
    for base,size in ((0x10000,0x100000),(0x20000000,0x40000),(0x40000000,0x30000),
                      (0x50000000,0x1000),(0xe000e000,0x2000)):m.mem_map(base,size)
    for base,data in segments:m.mem_write(base,data)
    # Constructors/full reset/bootloader are intentionally outside this test.
    latch=0;direction=[0]*32;direction[14]=int(inherited)
    trace=[];high=[]
    def write(uc,access,address,size,value,_):
        nonlocal latch
        trace.append((address,value))
        if address==0x50000508:latch|=value
        elif address==0x5000050c:latch&=~value
        elif 0x50000700<=address<0x50000780:direction[(address-0x50000700)//4]=value&1
        if direction[14] and latch&(1<<14):high.append((address,value))
    m.hook_add(UC_HOOK_MEM_WRITE,write,begin=0x50000000,end=0x50000fff)
    def call(symbol):
        m.reg_write(UC_ARM_REG_SP,0x2003fff0);m.reg_write(UC_ARM_REG_LR,0x10001)
        m.emu_start(symbols[symbol]|1,0x10000,count=100000)
        assert m.reg_read(UC_ARM_REG_PC)==0x10000
    call('init');before=len(high);split=len(trace)
    assert any(a==0x50000508 and v==0xffffffff for a,v in trace)
    call('_Z11initVariantv')
    delta=len(high)-before
    if profile=='tacticpod_reusable_usb':
        assert trace[split]==(0x5000050c,1<<14)
        assert trace[split+1][0]==0x50000738 and trace[split+1][1]&1
        assert delta==0 and not latch&(1<<14) and direction[14]
    else:assert delta>0 and latch&(1<<14) and direction[14]
    cnf13=struct.unpack('<I',m.mem_read(0x50000734,4))[0]
    assert not cnf13&1 and not cnf13&(3<<2) # INPUT and no pulls.
    assert not any(a==0x5000077c for a,v in trace) # No ADC pin direction changes.
    if inherited:assert before>0 # Earlier core OUTSET hazard is NOT hidden.
    return {'profile':profile,'inherited_output':inherited,
        'elf_sha256':hashlib.sha256(path.read_bytes()).hexdigest(),
        'core_high_observations':before,'variant_high_observations':delta,
        'P0.13_PIN_CNF':cnf13,'gpio_writes':trace}

results=[run('tacticpod_reusable_usb'),run('seeed_xiao_nrf52840_sense'),
         run('tacticpod_reusable_usb',True)]
(out/'compiled-startup-results.json').write_text(json.dumps(results,indent=2)+'\n')
for r in results:print('PASS:',r['profile'],'inherited=',r['inherited_output'],
    'core_high=',r['core_high_observations'],'variant_high=',r['variant_high_observations'])
print('PASS: linked core/variant, charger INPUT/no pulls, original variant negative control')
