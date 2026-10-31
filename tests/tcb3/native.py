from pathlib import Path
import subprocess,sys,struct
sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[2]
sys.path[:0]=[str(ROOT/'.pio-core/test-python'),str(ROOT/'tools')]
from elftools.elf.elffile import ELFFile
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_THUMB,UC_MODE_MCLASS
from unicorn.arm_const import UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_R0,UC_ARM_REG_PC

def compile(source,name,includes=(),extra=()):
    out=ROOT/('.pio/'+name+'.elf')
    command=[str(ROOT/'.pio-core/packages/toolchain-gccarmnoneeabi/bin/arm-none-eabi-g++.exe'),
       '-std=c++11','-Ofast','-DARDUINO=10819','-mcpu=cortex-m4','-mthumb','-fno-exceptions','-fno-rtti','-fno-unwind-tables',
       '-ffunction-sections','-fdata-sections','-nostartfiles','-fstack-usage',
       *['-I'+str(ROOT/p) for p in includes],str(ROOT/source),*map(str,extra),
       '-Wl,-Ttext=0x10000','-Wl,-Tdata=0x20000000','-lm','-o',str(out)]
    result=subprocess.run(command,cwd=ROOT/'.pio',capture_output=True)
    if result.returncode:
        print(result.stderr.decode('utf-8','replace'));result.check_returncode()
    return out

class Native:
    def __init__(self,path):
        with path.open('rb') as f:
            e=ELFFile(f);self.s={s.name:s['st_value'] for s in e.get_section_by_name('.symtab').iter_symbols()}
            loads=[(s['p_vaddr'],s.data()) for s in e.iter_segments() if s['p_type']=='PT_LOAD']
            init=e.get_section_by_name('.init_array');constructors=struct.unpack('<'+'I'*(init.data_size//4),init.data()) if init else []
        self.m=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS)
        self.m.mem_map(0x10000,0x100000);self.m.mem_map(0x20000000,0x4000000)
        for at,data in loads:self.m.mem_write(at,data)
        for fn in constructors:self.call(fn)
    def call(self,name,arg=0,limit=2000000000):
        m=self.m;m.reg_write(UC_ARM_REG_SP,0x23fff000);m.reg_write(UC_ARM_REG_LR,0xf0001);m.reg_write(UC_ARM_REG_R0,arg)
        m.emu_start((self.s[name] if isinstance(name,str) else name)|1,0xf0000,count=limit)
        assert m.reg_read(UC_ARM_REG_PC)==0xf0000,('instruction budget',name)
        return m.reg_read(UC_ARM_REG_R0)
    def put(self,name,data):self.m.mem_write(self.s[name],bytes(data))
    def get(self,name,n):return bytes(self.m.mem_read(self.s[name],n))
    def word(self,name,value=None):
        if value is not None:self.put(name,struct.pack('<I',value))
        return struct.unpack('<I',self.get(name,4))[0]
