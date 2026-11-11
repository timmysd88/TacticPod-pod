"""Emulate the linked preinit hook and checkpoint against retained RAM."""
import os
from pathlib import Path
import struct
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS
from unicorn.arm_const import UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_R0, UC_ARM_REG_SP

root = Path(__file__).resolve().parents[2]
build = Path(os.environ.get('PLATFORMIO_BUILD_DIR', root / '.pio/build'))
with (build / 'tacticpod_reusable_ble_boot_diagnostics' / 'firmware.elf').open('rb') as stream:
    elf = ELFFile(stream)
    segments = [(s['p_vaddr'], s.data()) for s in elf.iter_segments()
                if s['p_type'] == 'PT_LOAD' and s['p_filesz']]
    symbols = {s.name: s['st_value'] for s in elf.get_section_by_name('.symtab').iter_symbols()}
    marker = elf.get_section_by_name('.ble_reset')['sh_addr']
    retained = elf.get_section_by_name('.boot_diagnostics')['sh_addr']
    data = elf.get_section_by_name('.data')
    preinit = struct.unpack_from('<I', data.data(), symbols['__preinit_array_start'] - data['sh_addr'])[0]

m = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
m.mem_map(0x20000, 0xE0000)
m.mem_map(0x20000000, 0x40000)
for address, contents in segments:
    m.mem_write(address, contents)
sentinel = b'\x12\x34\x56\x78\x87\x65\x43\x21'
m.mem_write(marker, sentinel)

def call(address, arg=0):
    m.reg_write(UC_ARM_REG_SP, 0x2003fff0)
    m.reg_write(UC_ARM_REG_LR, 0x20001)
    m.reg_write(UC_ARM_REG_R0, arg)
    m.emu_start(address | 1, 0x20000, count=10000)
    assert m.reg_read(UC_ARM_REG_PC) == 0x20000

def slots():
    return struct.unpack('<IIII', m.mem_read(retained, 16))

def valid(value, inverse, stage):
    return value == (0x42544400 ^ stage) and inverse == (~value & 0xffffffff)

call(preinit)
assert valid(*slots()[:2], 1) and slots()[2:] == (0, 0)
call(symbols['_ZN15BootDiagnostics10checkpointENS_5StageE'], 8)
assert valid(*slots()[:2], 8)
call(preinit)
assert valid(*slots()[:2], 1) and valid(*slots()[2:], 8)
assert bytes(m.mem_read(marker, 8)) == sentinel
print('PASS: linked preinit/checkpoint across simulated reset; BLE marker unchanged')
