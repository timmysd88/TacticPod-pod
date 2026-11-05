"""Run actual recorder ARM code against mock NOR; never touches hardware."""
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / '.pio-core/test-python'))
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS
from unicorn.arm_const import UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_R0, UC_ARM_REG_PC

out = root / '.pio/recording-tests.elf'
compiler = root / '.pio-core/packages/toolchain-gccarmnoneeabi/bin/arm-none-eabi-g++.exe'
subprocess.run([str(compiler), '-std=c++11', '-mcpu=cortex-m4', '-mthumb', '-O1',
                '-fno-exceptions', '-fno-rtti', '-fno-unwind-tables', '-nostartfiles',
                '-I' + str(Path(__file__).parent), '-I' + str(root / 'tests/flash_write'),
                str(Path(__file__).with_name('harness.cpp')),
                '-Wl,-Ttext=0x10000', '-Wl,-Tdata=0x20000000', '-Wl,-e,runCase',
                '-o', str(out)], check=True)
with out.open('rb') as stream:
    elf = ELFFile(stream)
    entry = elf.get_section_by_name('.symtab').get_symbol_by_name('runCase')[0]['st_value']
    loads = [(s['p_vaddr'], s.data()) for s in elf.iter_segments() if s['p_type'] == 'PT_LOAD']
names = ['startup does not write; confirmation cannot be bypassed',
         '100 Hz input, sector rollover, STOP flush, append/reboot and CSV export',
         'power interruption: torn marker rejected; prior committed data survives',
         'torn session header reserves occupied sector',
         'blank header with old payload refuses overwrite', 'denied WEL locks recorder',
         'program timeout locks recorder', 'RAM overflow stops and flushes retained samples',
         'full area stops before diagnostic sector', 'erase requires fresh explicit confirmation',
         'diagnostic/recorder mutual exclusion', 'readback corruption prevents commit',
         'wrong JEDEC prevents writes', 'empty session closes safely',
         'main loop and command integration: 100 Hz recording with USB disconnected',
         'confirmed erase stops before reserved diagnostic sector',
         'payload corruption rejected despite valid marker', 'erase timeout lockout',
         'quiet after one startup report, late USB scan notice, one-shot DIAG',
         'DIAG refused during recording/export/fault/diagnostic ownership',
         'IMU ranges, register readback, ODR and live-unit conversions']
for index, name in enumerate(names):
    machine = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
    machine.mem_map(0x10000, 0x100000)
    machine.mem_map(0x20000000, 0x200000)
    for address, data in loads:
        machine.mem_write(address, data)
    machine.reg_write(UC_ARM_REG_SP, 0x201FF000)
    machine.reg_write(UC_ARM_REG_LR, 0xF0001)
    machine.reg_write(UC_ARM_REG_R0, index)
    machine.emu_start(entry | 1, 0xF0000, count=100000000)
    if machine.reg_read(UC_ARM_REG_PC) != 0xF0000:
        raise SystemExit(f'FAIL: {name} (instruction budget exhausted)')
    result = machine.reg_read(UC_ARM_REG_R0)
    if result:
        raise SystemExit(f'FAIL: {name} (assertion {result})')
    print('PASS:', name)
