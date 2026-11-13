"""Validate the real opt-in ELF layout; never execute firmware or access hardware."""
from pathlib import Path
import os
from elftools.elf.elffile import ELFFile

root = Path(__file__).resolve().parents[2]
for name in ('seeed_xiao_nrf52840_sense', 'tacticpod_reusable_usb', 'tacticpod_reusable_ble'):
    path = Path(os.environ.get('PLATFORMIO_BUILD_DIR', root / '.pio/build')) / name / 'firmware.elf'
    with path.open('rb') as stream:
        elf = ELFFile(stream)
        text = elf.get_section_by_name('.text')
        assert text['sh_addr'] == 0x27000, (name, 'application/SoftDevice boundary')
        marker = elf.get_section_by_name('.ble_reset')
        assert elf.get_section_by_name('.boot_diagnostics') is None, (name, 'unexpected boot diagnostics')
        if name in ('tacticpod_reusable_usb', 'tacticpod_reusable_ble'):
            buffers = [s for s in elf.get_section_by_name('.symtab').iter_symbols()
                       if 'readPage' in s.name and 'txBuffer' in s.name]
            assert len(buffers) == 1, (name, 'page DMA TX buffer missing')
            buffer = buffers[0]
            section = elf.get_section(buffer['st_shndx'])
            assert buffer['st_size'] == 256 and section['sh_type'] == 'SHT_NOBITS'
            assert section['sh_flags'] & 1  # Writable BSS is zero-filled RAM, not flash.
            assert 0x20000000 <= buffer['st_value'] <= 0x20040000 - 256
            print('PASS:', name, '256-byte page DMA TX buffer in zero-filled writable device SRAM')
        if name != 'tacticpod_reusable_ble':
            assert marker is None, (name, 'unexpected BLE reset marker')
            continue
        assert marker is not None and marker['sh_type'] == 'SHT_NOBITS'
        assert marker['sh_size'] == 8
        bss = elf.get_section_by_name('.bss')
        heap = elf.get_section_by_name('.heap')
        assert bss['sh_addr'] + bss['sh_size'] <= marker['sh_addr']
        assert marker['sh_addr'] + marker['sh_size'] <= heap['sh_addr']
        assert 0x20006000 <= marker['sh_addr'] < 0x2003F800
        for segment in elf.iter_segments():
            if segment['p_type'] == 'PT_LOAD' and segment['p_filesz']:
                assert segment['p_paddr'] + segment['p_filesz'] <= 0xED000
        symbols = elf.get_section_by_name('.symtab')
        assert symbols.get_symbol_by_name('__flash_arduino_start')[0]['st_value'] == 0x27000
        assert symbols.get_symbol_by_name('__flash_arduino_end')[0]['st_value'] == 0xED000
        print('PASS: BLE ELF application boundary and 8-byte NOLOAD marker between BSS and heap')
