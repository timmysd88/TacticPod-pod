"""Verify the diagnostic application-only DFU archive against its ELF."""
import binascii
import json
import os
from pathlib import Path
import struct
from zipfile import ZipFile
from elftools.elf.elffile import ELFFile

root = Path(__file__).resolve().parents[2]
build = Path(os.environ.get('PLATFORMIO_BUILD_DIR', root / '.pio/build'))
folder = build / 'tacticpod_reusable_ble_boot_diagnostics'
with ZipFile(folder / 'firmware.zip') as archive:
    assert archive.testzip() is None
    assert set(archive.namelist()) == {'manifest.json', 'firmware.dat', 'firmware.bin'}
    manifest = json.loads(archive.read('manifest.json'))['manifest']
    assert set(manifest) == {'application', 'dfu_version'} and manifest['dfu_version'] == 0.5
    app = manifest['application']
    assert app['bin_file'] == 'firmware.bin' and app['dat_file'] == 'firmware.dat'
    binary = archive.read(app['bin_file'])
    packet = archive.read(app['dat_file'])
    device, revision, version, count, sd, crc = struct.unpack('<HHIHHH', packet)
    assert (device, revision, version, count, sd) == (0x52, 0xffff, 0xffffffff, 1, 0x123)
    assert crc == binascii.crc_hqx(binary, 0xffff)
    info = app['init_packet_data']
    assert (info['device_type'], info['device_revision'], info['application_version'],
            info['softdevice_req'], info['firmware_crc16']) == (device, revision, version, [sd], crc)
    with (folder / 'firmware.elf').open('rb') as stream:
        elf = ELFFile(stream)
        expected = bytearray(b'\xff' * len(binary))
        for section in elf.iter_sections():
            if not section['sh_flags'] & 2 or section['sh_type'] == 'SHT_NOBITS' or not section['sh_size']:
                continue
            loads = [s for s in elf.iter_segments() if s['p_type'] == 'PT_LOAD' and
                     s.section_in_segment(section)]
            assert len(loads) == 1
            offset = loads[0]['p_paddr'] + section['sh_addr'] - loads[0]['p_vaddr'] - 0x27000
            assert 0 <= offset and offset + section['sh_size'] <= len(expected)
            expected[offset:offset + section['sh_size']] = section.data()
        assert binary == expected
        assert 0x27000 + len(binary) <= 0xED000
    print(f'PASS: application-only DFU, ELF payload, ZIP CRC and CRC16 0x{crc:04x} ({len(binary)} bytes)')
