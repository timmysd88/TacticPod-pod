"""Approved read-only USB diagnostics. Never starts/stops or changes settings."""
import hashlib
import json
import pathlib
import re
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
OUT = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / 'tools'))
from tcb3_download import capture
from tcb3_decode import decode_packets, decode_image
import serial
from serial.tools import list_ports

ALLOWED = {'BAT STATUS', 'TCB STATUS', 'RECORD STATUS', 'OBS STATUS',
           'COMP STATUS', 'GPS STATUS', 'DIAG ID', 'GPS RATE'}

def main():
    ports = [p for p in list_ports.comports() if
             (p.vid, p.pid, p.serial_number) == (0x2886, 0x8044, '0E8A89B7289430BD')]
    if len(ports) != 1:
        raise RuntimeError('Expected exactly one identified application USB port')
    result = {'port': ports[0].device, 'commands': [], 'serial_released': False,
              'new_recording_started': False, 'settings_changed': False}
    # Exclusive files preserve evidence and prevent accidental reruns.
    with (OUT / 'transcript.txt').open('xb') as log:
        try:
            with serial.Serial(ports[0].device, 115200, timeout=.03, write_timeout=1) as link:
                def read(seconds, marker=None):
                    end = time.monotonic() + seconds
                    data = bytearray()
                    while time.monotonic() < end:
                        b = link.read(max(1, link.in_waiting))
                        if b:
                            log.write(b); log.flush(); data.extend(b)
                        if marker and marker.encode() in data:
                            pos = data.index(marker.encode())
                            if b'\n' in data[pos:]:
                                break
                    text = data.decode('utf-8', 'replace')
                    if marker and marker not in text:
                        raise RuntimeError('Missing response ' + marker + ': ' + text)
                    return text
                def command(cmd, marker, seconds=8):
                    assert cmd in ALLOWED
                    result['commands'].append(cmd)
                    log.write(('\nHOST TX ' + cmd + '\n').encode()); log.flush()
                    link.write((cmd + '\n').encode()); link.flush()
                    return read(seconds, marker)
                result['reconnect_output'] = read(3)
                result['states'] = {}
                for cmd, marker in [('BAT STATUS', 'BAT source='),
                                    ('TCB STATUS', 'TCB STATUS'),
                                    ('RECORD STATUS', 'RECORD STATUS:'),
                                    ('OBS STATUS', 'OBS END'),
                                    ('COMP STATUS', 'COMP END'),
                                    ('GPS STATUS', 'GPS speed_')]:
                    result['states'][cmd] = command(cmd, marker)
                states = result['states']
                assert 'state=1 ' in states['TCB STATUS'] and 'lock=0' in states['TCB STATUS'] and 'error=none' in states['TCB STATUS'], states
                assert 'RECORD STATUS: STOPPED' in states['RECORD STATUS'], states
                assert 'capture=OFF' in states['OBS STATUS'] and 'RAM_fast_imu=OFF' in states['OBS STATUS'], states
                assert 'capturing=0 fault=0' in states['COMP STATUS'] and 'input=0/2 output=0/2' in states['COMP STATUS'], states
                result['identity'] = command('DIAG ID', 'END_FLASH_ID', 12)
                assert re.findall(r'FLASH ID JEDEC \d+ ([0-9A-F]+)', result['identity']) == ['0B4018'] * 5
                assert 'SR1 before=00 after=00 SR2=02' in result['identity']
                assert 'JEDEC_repeat=PASS SFDP_repeat=PASS' in result['identity']
                reference = (ROOT / 'logs/phase3b4-hardware/sfdp-capture-1.bin').read_bytes()
                parts = re.findall(r'FLASH ID SFDP pass=(\d+) address=([0-9A-F]+) ([0-9A-F]+)', result['identity'])
                assert len(parts) == 32
                assert all(bytes.fromhex(h) == reference[int(a,16):int(a,16)+16] for _, a, h in parts)
                result['gps_rate'] = command('GPS RATE', 'GPS RATE END', 22)
                result['battery_after'] = command('BAT STATUS', 'BAT source=')
                result['gps_after'] = command('GPS STATUS', 'GPS speed_')
                result['exports'] = []
                for name in ('baseline-1.tpx3', 'baseline-2.tpx3'):
                    read(.3)
                    with (OUT / name).open('xb') as target:
                        result['commands'].append('TCB READ')
                        log.write(b'\nHOST TX TCB READ\n'); log.flush()
                        result['exports'].append(capture(link, target, timeout=90))
                result['final_tcb'] = command('TCB STATUS', 'TCB STATUS')
                result['final_battery'] = command('BAT STATUS', 'BAT source=')
            result['serial_released'] = True
            binaries = [(OUT / n).read_bytes() for n in ('baseline-1.tpx3', 'baseline-2.tpx3')]
            assert binaries[0] == binaries[1], 'Duplicate exports differ'
            archive = (ROOT / 'logs/switchless-usb-hardware/after-1.tpx3').read_bytes()
            result['archive_byte_identical'] = binaries[0] == archive
            assert result['archive_byte_identical'], 'Export differs from last verified archive'
            result['inventory'] = []
            for binary in binaries:
                image, issues = decode_packets(binary)
                assert not issues, issues
                doc = decode_image(image)
                assert not doc['issues'], doc['issues']
                assert [s['session_id'] for s in doc['sessions']] == [1, 3, 12, 58]
                inventory = []
                for s in doc['sessions']:
                    assert s['status'] == 'complete' and not s['issues'], s['issues']
                    isolated = bytearray(b'\xff' * len(image))
                    for a in s['sectors']:
                        isolated[a:a+4096] = image[a:a+4096]
                    independent = decode_image(bytes(isolated))
                    assert not independent['issues'] and len(independent['sessions']) == 1
                    assert independent['sessions'][0]['raw'] == s['raw']
                    inventory.append(dict(session_id=s['session_id'], status=s['status'],
                                          sectors=s['sectors'], footer=s['footer'],
                                          container_version=s.get('container_version')))
                result['inventory'].append(inventory)
            result['verified'] = True
            print(json.dumps(result, indent=2))
        finally:
            result['serial_released'] = not ('link' in locals() and link.is_open)
            (OUT / 'result.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')

if __name__ == '__main__':
    main()
