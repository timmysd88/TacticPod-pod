"""Run linked Bluefruit characteristic initialization with a constrained SoftDevice model.

The SVC model enforces the documented writable-CCCD rule. It is not the radio
or the proprietary SoftDevice and cannot qualify its resource consumption.
"""
import os
from pathlib import Path
import re
import struct
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_CODE, UC_HOOK_INTR
from unicorn.arm_const import UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_SP
from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3

root = Path(__file__).resolve().parents[2]
build = Path(os.environ.get('PLATFORMIO_BUILD_DIR', root / '.pio/build'))
source = (root / 'src/ble_runtime.cpp').read_text()
properties = {'WRITE': 8, 'READ': 2, 'NOTIFY': 16}
permissions = {'SECMODE_OPEN': 0x11, 'SECMODE_NO_ACCESS': 0}
expected = {
    'activity': ('WRITE', 'SECMODE_NO_ACCESS', 'SECMODE_OPEN', False, 20),
    'battery': ('READ', 'SECMODE_OPEN', 'SECMODE_NO_ACCESS', True, 6),
    'control': ('WRITE', 'SECMODE_NO_ACCESS', 'SECMODE_OPEN', False, 16),
    'data': ('NOTIFY', 'SECMODE_OPEN', 'SECMODE_NO_ACCESS', False, 244),
    'protocolVersion': ('READ', 'SECMODE_OPEN', 'SECMODE_NO_ACCESS', True, 2),
    'performance': ('READ', 'SECMODE_OPEN', 'SECMODE_NO_ACCESS', True, 176),
    'linkPerformance': ('READ', 'SECMODE_OPEN', 'SECMODE_NO_ACCESS', True, 16),
    'speedCapability': ('READ', 'SECMODE_OPEN', 'SECMODE_NO_ACCESS', True, 16),
    'speedAck': ('WRITE', 'SECMODE_NO_ACCESS', 'SECMODE_OPEN', True, 12),
}
constants = {'sizeof(controlQueue)': 16, 'kMaxFrameSize': 244, 'BlePerformance::SnapshotSize': 176,
             'BlePerformance::LinkSnapshotSize': 16, 'sizeof(speedSnapshot)': 16,
             'BleHighSpeed::AckSize': 12}
for name, (prop, read, write, fixed, length) in expected.items():
    assert re.search(rf'{name}\.setProperties\(CHR_PROPS_{prop}\)', source)
    match = re.search(rf'{name}\.setPermission\((\w+),\s*(\w+)\)', source)
    assert match and match.groups() == (read, write), (name, 'permission contract')
    match = re.search(rf'{name}\.set(Max|Fixed)Len\((.*?)\);', source)
    assert match and (match[1] == 'Fixed') == fixed
    assert constants.get(match[2], match[2]) in (length, str(length))

uuids = re.findall(r'constexpr const char\* k\w+Uuid = "([^"]+)"', source)
assert uuids == [f'b973000{i:x}-7d2a-4f91-a380-8b52c029dc61' for i in range(1, 11)]
bases = {bytes.fromhex(u.replace('-', ''))[:2] + bytes.fromhex(u.replace('-', ''))[4:] for u in uuids}
assert len(bases) == 1  # Only the SoftDevice's 16-bit UUID field changes.

for profile in ('tacticpod_reusable_ble',):
    with (build / profile / 'firmware.elf').open('rb') as stream:
        elf = ELFFile(stream)
        symbols = {s.name: s['st_value'] for s in elf.get_section_by_name('.symtab').iter_symbols()}
        loads = [(s['p_vaddr'], s.data()) for s in elf.iter_segments()
                 if s['p_type'] == 'PT_LOAD' and s['p_filesz']]
    m = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
    m.mem_map(0x20000, 0xE0000)
    m.mem_map(0x20000000, 0x40000)
    for address, contents in loads:
        m.mem_write(address, contents)
    observed = []

    def finish(value):
        m.reg_write(UC_ARM_REG_R0, value)
        m.reg_write(UC_ARM_REG_PC, m.reg_read(UC_ARM_REG_LR))

    def hook(uc, address, size, _):
        if address == (symbols['_ZN7BLEUuid5beginEv'] & ~1):
            finish(1)
        elif address == (symbols['_ZN10BLEService13getPermissionEP12SecureMode_tS1_'] & ~1):
            m.mem_write(m.reg_read(UC_ARM_REG_R1), b'\x11')
            m.mem_write(m.reg_read(UC_ARM_REG_R2), b'\x11')
            finish(0)
        elif address == (symbols['_ZN7BLEGatt18_addCharacteristicEP17BLECharacteristic'] & ~1):
            finish(1)

    def svc(uc, interrupt, _):
        assert interrupt == 2, ('unexpected exception', interrupt)
        assert bytes(m.mem_read(m.reg_read(UC_ARM_REG_PC) - 2, 2)) == b'\xaa\xdf'
        md = m.reg_read(UC_ARM_REG_R1)
        attr = m.reg_read(UC_ARM_REG_R2)
        prop = m.mem_read(md, 1)[0]
        cccd = struct.unpack('<I', m.mem_read(md + 20, 4))[0]
        meta = struct.unpack('<I', m.mem_read(attr + 4, 4))[0]
        read, write, flags = m.mem_read(meta, 3)
        init_len, offset, maximum = struct.unpack('<HHH', m.mem_read(attr + 8, 6))
        observed.append((prop, read, write, flags & 1, init_len, maximum, cccd))
        error = 0
        if prop & 16:
            assert cccd
            cccd_read, cccd_write = m.mem_read(cccd, 2)
            if cccd_read != 0x11 or cccd_write == 0:
                error = 7  # NRF_ERROR_INVALID_PARAM, documented CCCD constraint.
        m.reg_write(UC_ARM_REG_R0, error)

    m.hook_add(UC_HOOK_CODE, hook)
    m.hook_add(UC_HOOK_INTR, svc)
    obj = 0x2003e000

    def call(symbol, *args):
        m.reg_write(UC_ARM_REG_SP, 0x2003f700)
        m.reg_write(UC_ARM_REG_LR, 0x20001)
        for reg, value in zip((UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3), args):
            m.reg_write(reg, value)
        m.emu_start(symbols[symbol] | 1, 0x20000, count=100000)
        assert m.reg_read(UC_ARM_REG_PC) == 0x20000, symbol
        return m.reg_read(UC_ARM_REG_R0)

    def initialize(prop, read, write, fixed, length):
        call('_ZN17BLECharacteristicC1Ev', obj)
        call('_ZN17BLECharacteristic13setPropertiesEh', obj, properties[prop])
        call('_ZN17BLECharacteristic13setPermissionE12SecureMode_tS0_', obj,
             permissions[read], permissions[write])
        setter = '_ZN17BLECharacteristic11setFixedLenEt' if fixed else '_ZN17BLECharacteristic9setMaxLenEt'
        call(setter, obj, length)
        return call('_ZN17BLECharacteristic5beginEv', obj)

    assert initialize('NOTIFY', 'SECMODE_NO_ACCESS', 'SECMODE_OPEN', False, 244) == 7
    print('PASS:', profile, 'original data characteristic reproduces invalid CCCD permissions')
    for name, config in expected.items():
        assert initialize(*config) == 0, name
        prop, read, write, variable, initial, maximum, cccd = observed[-1]
        assert maximum == config[4] and variable == (not config[3])
        assert initial == (config[4] if config[3] else 0)
        if name == 'data':
            assert prop == 16 and read == 0x11 and write == 0
            assert bytes(m.mem_read(cccd, 2)) == b'\x11\x11'
        print('PASS:', profile, name, 'linked Bluefruit begin, lengths and permissions')
