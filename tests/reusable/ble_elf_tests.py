"""Validate the real opt-in ELF layout; never execute firmware or access hardware."""
from pathlib import Path
import os
import struct
from elftools.elf.elffile import ELFFile

root = Path(__file__).resolve().parents[2]
build = Path(os.environ.get('PLATFORMIO_BUILD_DIR', root / '.pio/build'))
for name in ('seeed_xiao_nrf52840_sense', 'tacticpod_reusable_usb', 'tacticpod_reusable_ble',
             'tacticpod_reusable_ble_boot_diagnostics'):
    path = build / name / 'firmware.elf'
    with path.open('rb') as stream:
        elf = ELFFile(stream)
        text = elf.get_section_by_name('.text')
        assert text['sh_addr'] == 0x27000, (name, 'application/SoftDevice boundary')
        symbols = elf.get_section_by_name('.symtab')
        symbol = lambda key: symbols.get_symbol_by_name(key)[0]['st_value']
        stack, reset = struct.unpack_from('<II', text.data())
        assert stack == 0x20040000 and reset == symbol('Reset_Handler')
        assert 0x27000 <= (reset & ~1) < 0xED000 and reset & 1
        assert symbol('__flash_arduino_start') == 0x27000
        assert symbol('__flash_arduino_end') == 0xED000
        for section in elf.iter_sections():
            if not section['sh_flags'] & 2 or section['sh_type'] == 'SHT_NOBITS' or not section['sh_size']:
                continue
            loads = [s for s in elf.iter_segments() if s['p_type'] == 'PT_LOAD' and
                     s.section_in_segment(section)]
            assert len(loads) == 1
            address = loads[0]['p_paddr'] + section['sh_addr'] - loads[0]['p_vaddr']
            assert 0x27000 <= address and address + section['sh_size'] <= 0xED000
        marker = elf.get_section_by_name('.ble_reset')
        diagnostic = elf.get_section_by_name('.boot_diagnostics')
        if name not in ('tacticpod_reusable_ble', 'tacticpod_reusable_ble_boot_diagnostics'):
            assert marker is None, (name, 'unexpected BLE reset marker')
            assert diagnostic is None, (name, 'unexpected diagnostic marker')
            continue
        assert marker is not None and marker['sh_type'] == 'SHT_NOBITS'
        assert marker['sh_size'] == 8
        bss = elf.get_section_by_name('.bss')
        heap = elf.get_section_by_name('.heap')
        assert bss['sh_addr'] + bss['sh_size'] <= marker['sh_addr']
        assert 0x20006000 <= marker['sh_addr']
        if name == 'tacticpod_reusable_ble':
            assert diagnostic is None
            assert marker['sh_addr'] + marker['sh_size'] <= heap['sh_addr']
        else:
            assert diagnostic is not None and diagnostic['sh_type'] == 'SHT_NOBITS'
            assert diagnostic['sh_size'] == 16
            assert marker['sh_addr'] + marker['sh_size'] <= diagnostic['sh_addr']
            assert diagnostic['sh_addr'] + diagnostic['sh_size'] <= heap['sh_addr']
            start = symbol('__preinit_array_start')
            end = symbol('__preinit_array_end')
            assert end - start == 4
            data = elf.get_section_by_name('.data')
            entry = struct.unpack_from('<I', data.data(), start - data['sh_addr'])[0]
            assert entry & 1 and 0x27000 <= (entry & ~1) < 0xED000
            assert symbol('__bss_end__') <= marker['sh_addr']
        assert heap['sh_addr'] <= 0x2003F800
        print('PASS:', name, 'vector, flash boundaries and retained marker isolation')
